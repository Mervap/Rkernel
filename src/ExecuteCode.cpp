//  Rkernel is an execution kernel for R interpreter
//  Copyright (C) 2019 JetBrains s.r.o.
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include "RPIServiceImpl.h"
#include <grpcpp/server_builder.h>
#include "IO.h"
#include "util/ScopedAssign.h"
#include "debugger/SourceFileManager.h"
#include "RStuff/RUtil.h"
#include "EventLoop.h"

static void executeCodeImpl(ShieldSEXP const& exprs, ShieldSEXP const& env, bool withEcho = true, bool isDebug = false,
                            bool withExceptionHandler = false);

static void exceptionToProto(ShieldSEXP const& e, ExceptionInfo *proto) {
  if (Rf_inherits(e, "interrupt")) {
    proto->mutable_interrupted();
    proto->set_message("Interrupted");
    return;
  }
  try {
    proto->set_message(asStringUTF8(RI->conditionMessage(e)));
  } catch (RError const&) {
    proto->set_message("Error");
  }
  if (Rf_inherits(e, "packageNotFoundError")) {
    std::string package = asStringUTF8(e["package"]);
    if (!package.empty()) {
      proto->set_packagenotfound(package);
      return;
    }
  }
  proto->mutable_simpleerror();
}

Status RPIServiceImpl::executeCode(ServerContext* context, const ExecuteCodeRequest* request, ServerWriter<ExecuteCodeResponse>* writer) {
  executeOnMainThread([&] {
    std::string const& code = request->code();
    std::string const& sourceFileId = request->sourcefileid();
    int sourceFileLineOffset = request->sourcefilelineoffset();
    bool withEcho = request->withecho();
    bool streamOutput = request->streamoutput() && writer != nullptr;
    bool isRepl = request->isrepl();
    bool isDebug = isRepl && request->isdebug();
    bool resetDebugCommand = isDebug && request->resetdebugcommand();

    ScopedAssign<bool> withIsReplOutput(isReplOutput, isRepl && !streamOutput);
    WithOutputHandler withOutputHandler(
        isReplOutput ? replOutputHandler : [&](const char* buf, int len, OutputType type) {
          if (streamOutput) {
            ExecuteCodeResponse response;
            response.mutable_output()->set_type(type == STDOUT ? CommandOutput::STDOUT : CommandOutput::STDERR);
            response.mutable_output()->set_text(buf, len);
            writer->Write(response);
          }
        });

    try {
      ScopedAssign<ReplState> withState(replState, isRepl ? REPL_BUSY : replState);
      if (isRepl) {
        AsyncEvent event;
        event.mutable_busy();
        asyncEvents.push(event);
        rDebugger.resetLastErrorStack();
        if (resetDebugCommand) rDebugger.setCommand(CONTINUE);
      }
      PrSEXP expressions;
      expressions = sourceFileManager.parseSourceFile(code, sourceFileId, sourceFileLineOffset);
      executeCodeImpl(expressions, currentEnvironment(), withEcho, isDebug, isRepl);
    } catch (RError const& e) {
      if (writer != nullptr) {
        ExecuteCodeResponse response;
        response.set_exception(e.what());
        writer->Write(response);
      }
      if (isRepl) {
        AsyncEvent event;
        exceptionToProto(e.getRError(), event.mutable_exception()->mutable_exception());
        lastErrorStack = rDebugger.getLastErrorStack();
        buildStackProto(lastErrorStack, event.mutable_exception()->mutable_stack());
        rpiService->sendAsyncEvent(event);
      } else {
        std::string msg = std::string("\n") + e.what() + "\n";
        myWriteConsoleEx(msg.c_str(), msg.size(), STDERR);
      }
    } catch (RInterruptedException const& e) {
      if (isRepl) {
        AsyncEvent event;
        event.mutable_exception()->mutable_exception()->set_message("Interrupted");
        event.mutable_exception()->mutable_exception()->mutable_interrupted();
        rpiService->sendAsyncEvent(event);
      }
      if (writer != nullptr) {
        ExecuteCodeResponse response;
        response.set_exception(e.what());
        writer->Write(response);
      }
    } catch (...) {
    }

    if (isRepl) {
      AsyncEvent event;
      if (replState == DEBUG_PROMPT) {
        event.mutable_debugprompt()->set_changed(false);
      } else {
        event.mutable_prompt();
        rDebugger.clearStack();
      }
      asyncEvents.push(event);
    }
  }, context);
  return Status::OK;
}

std::string RPIServiceImpl::readLineHandler(std::string const& prompt) {
  if (replState != REPL_BUSY) return "";
  AsyncEvent event;
  event.mutable_requestreadln()->set_prompt(prompt);
  asyncEvents.push(event);
  ScopedAssign<ReplState> withState(replState, READ_LINE);
  std::string result = runEventLoop();
  event.mutable_busy();
  asyncEvents.push(event);
  return result;
}

void RPIServiceImpl::debugPromptHandler() {
  if (replState != REPL_BUSY) return;
  AsyncEvent event;
  rDebugger.buildDebugPrompt(event.mutable_debugprompt());
  asyncEvents.push(event);
  ScopedAssign<ReplState> withState(replState, DEBUG_PROMPT);
  runEventLoop();
  event.mutable_busy();
  asyncEvents.push(event);
}

Status RPIServiceImpl::executeCommand(ServerContext* context, const std::string& command, ServerWriter<CommandOutput>* writer) {
  executeOnMainThread([&] {
    WithOutputHandler withOutputHandler([&](const char* buf, int len, OutputType type) {
      CommandOutput response;
      response.set_type(type == STDOUT ? CommandOutput::STDOUT : CommandOutput::STDERR);
      response.set_text(buf, len);
      writer->Write(response);
    });
    try {
      ShieldSEXP expressions = parseCode(command);
      executeCodeImpl(expressions, currentEnvironment(), true, false, false);
    } catch (RError const& e) {
      std::string s = std::string("\n") + e.what() + '\n';
      myWriteConsoleEx(s.c_str(), s.size(), STDERR);
    }
  }, context);
  return Status::OK;
}

Status RPIServiceImpl::replInterrupt(ServerContext*, const Empty*, Empty*) {
  if (replState == REPL_BUSY || isEventHandlerRunning()) {
    R_interrupts_pending = 1;
  } else if (replState == READ_LINE) {
    eventLoopExecute([=] {
      if (replState == READ_LINE) {
        R_interrupts_pending = 1;
        breakEventLoop("");
      }
    });
  } else if (replState == SUBPROCESS_INPUT && subprocessActive) {
    eventLoopExecute([=] {
      if (replState == SUBPROCESS_INPUT && subprocessActive) {
        subprocessInterrupt = true;
        breakEventLoop("");
      }
    });
  }
  return Status::OK;
}

Status RPIServiceImpl::replExecuteCommand(ServerContext* context, const std::string& command) {
  ExecuteCodeRequest request;
  request.set_code(command);
  request.set_isrepl(true);
  request.set_withecho(true);
  return executeCode(context, &request, nullptr);
}

Status RPIServiceImpl::sendReadLn(ServerContext*, const StringValue* request, Empty*) {
  std::string text = request->value();
  eventLoopExecute([=] () mutable {
    if (replState == READ_LINE) {
      text.erase(std::find(text.begin(), text.end(), '\n'), text.end());
      breakEventLoop(text);
    } else if (replState == SUBPROCESS_INPUT && !text.empty()) {
      breakEventLoop(text);
    }
  });
  return Status::OK;
}

OutputHandler RPIServiceImpl::getOutputHandlerForChildProcess() {
  if (isReplOutput) {
    return replOutputHandler;
  } else {
    int id = getCurrentOutputHandlerId();
    return [=](const char* buf, int len, OutputType type) {
      return myWriteConsoleExToSpecificHandler(buf, len, type, id);
    };
  }
}

static SEXP cloneSrcref(SEXP srcref) {
  SEXP newSrcref = Rf_allocVector(INTSXP, Rf_length(srcref));
  memcpy(INTEGER(newSrcref), INTEGER(srcref), sizeof(int) * Rf_length(srcref));
  Rf_setAttrib(newSrcref, RI->srcfileAttr, Rf_getAttrib(srcref, RI->srcfileAttr));
  Rf_setAttrib(newSrcref, R_ClassSymbol, Rf_mkString("srcref"));
  return newSrcref;
}

static SEXP wrapWithSrcref(PrSEXP forEval, ShieldSEXP const& srcref, bool isPrint = false) {
  if (srcref != R_NilValue) {
    forEval = Rf_lang2(Rf_install("{"), forEval);
    ShieldSEXP newSrcrefs = Rf_allocVector(VECSXP, 2);
    SET_VECTOR_ELT(newSrcrefs, 0, cloneSrcref(srcref));
    if (isPrint) {
      ShieldSEXP newSrcref = cloneSrcref(srcref);
      Rf_setAttrib(newSrcref, RI->doNotStopFlag, toSEXP(true));
      SET_VECTOR_ELT(newSrcrefs, 1, newSrcref);
    } else {
      SET_VECTOR_ELT(newSrcrefs, 1, srcref);
    }
    Rf_setAttrib(forEval, RI->srcrefAttr, newSrcrefs);
  }
  return RI->expression(forEval);
}

static void executeCodeImpl(ShieldSEXP const& exprs, ShieldSEXP const& env, bool withEcho, bool isDebug,
    bool withExceptionHandler) {
  if (exprs.type() != EXPRSXP || env.type() != ENVSXP) {
    return;
  }
  int length = exprs.length();
  ShieldSEXP srcrefs = getBlockSrcrefs(exprs);
  for (int i = 0; i < length; ++i) {
    PrSEXP forEval = exprs[i];
    ShieldSEXP srcref = getSrcref(srcrefs, i);
    forEval = wrapWithSrcref(forEval, srcref);
    forEval = Rf_lang4(RI->wrapEval, forEval, env, toSEXP(isDebug));
    if (withExceptionHandler) {
      forEval = Rf_lang2(RI->withReplExceptionHandler, forEval);
    }
    if (withEcho) {
      forEval = Rf_lang2(RI->withVisible, forEval);
    }
    PrSEXP result = safeEval(forEval, R_GlobalEnv);
    if (withEcho && asBool(result["visible"])) {
      forEval = Rf_lang2(Rf_install("print"), result["value"]);
      forEval = wrapWithSrcref(forEval, srcref, true);
      forEval = Rf_lang4(RI->printWrapper, forEval, env, toSEXP(isDebug));
      if (withExceptionHandler) {
        forEval = Rf_lang2(RI->withReplExceptionHandler, forEval);
      }
      safeEval(forEval, env);
    }
  }
}
