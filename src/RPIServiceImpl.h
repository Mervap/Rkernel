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


#ifndef RWRAPPER_RPI_SERVICE_IMPL_H
#define RWRAPPER_RPI_SERVICE_IMPL_H

#include "protos/service.grpc.pb.h"
#include <string>
#include <functional>
#include "util/BlockingQueue.h"
#include "util/IndexedStorage.h"
#include "IO.h"
#include "Options.h"
#include "debugger/RDebugger.h"
#include "RStuff/MySEXP.h"

using grpc::Status;
using grpc::ServerContext;
using grpc::ServerWriter;
using namespace rplugininterop;
using namespace google::protobuf;

class RPIServiceImpl : public RPIService::Service {
public:
  RPIServiceImpl();
  ~RPIServiceImpl() override;

  Status getInfo(ServerContext* context, const Empty*, GetInfoResponse* response) override;
  Status isBusy(ServerContext* context, const Empty*, BoolValue* response) override;
  Status init(ServerContext* context, const Init* request, ServerWriter<CommandOutput>* response) override;
  Status quit(ServerContext* context, const Empty*, Empty*) override;
  Status quitProceed(ServerContext* context, const Empty*, Empty*) override;

  Status executeCode(ServerContext* context, const ExecuteCodeRequest* request, ServerWriter<ExecuteCodeResponse>* writer) override;
  Status sendReadLn(ServerContext* context, const StringValue* request, Empty*) override;
  Status sendEof(ServerContext* context, const Empty*, Empty*) override;
  Status replInterrupt(ServerContext* context, const Empty*, Empty*) override;
  Status getNextAsyncEvent(ServerContext* context, const Empty*, AsyncEvent* response) override;

  Status debugAddBreakpoint(ServerContext* context, const DebugAddBreakpointRequest* request, Empty*) override;
  Status debugRemoveBreakpoint(ServerContext* context, const SourcePosition* request, Empty*) override;
  Status debugCommandContinue(ServerContext* context, const Empty*, Empty*) override;
  Status debugCommandPause(ServerContext* context, const Empty*, Empty*) override;
  Status debugCommandStop(ServerContext* context, const Empty*, Empty*) override;
  Status debugCommandStepOver(ServerContext* context, const Empty*, Empty*) override;
  Status debugCommandStepInto(ServerContext* context, const Empty*, Empty*) override;
  Status debugCommandForceStepInto(ServerContext* context, const Empty*, Empty*) override;
  Status debugCommandStepOut(ServerContext* context, const Empty*, Empty*) override;
  Status debugCommandRunToPosition(ServerContext* context, const SourcePosition* request, Empty*) override;
  Status debugMuteBreakpoints(ServerContext* context, const BoolValue* request, Empty*) override;

  Status copyToPersistentRef(ServerContext* context, const RRef* request, CopyToPersistentRefResponse* response) override;
  Status disposePersistentRefs(ServerContext* context, const PersistentRefList* request, Empty*) override;

  Status loaderGetParentEnvs(ServerContext* context, const RRef* request, ParentEnvsResponse* response) override;
  Status loaderGetVariables(ServerContext* context, const GetVariablesRequest* request, VariablesResponse* response) override;
  Status loaderGetLoadedNamespaces(ServerContext* context, const Empty*, StringList* response) override;
  Status loaderGetValueInfo(ServerContext* context, const RRef* request, ValueInfo* response) override;
  Status evaluateAsText(ServerContext* context, const RRef* request, StringOrError* response) override;
  Status evaluateAsBoolean(ServerContext* context, const RRef* request, BoolValue* response) override;
  Status getDistinctStrings(ServerContext* context, const RRef* request, StringList* response) override;
  Status getFunctionSourcePosition(ServerContext* context, const RRef* request, SourcePosition* response) override;
  Status getSourceFileText(ServerContext* context, const StringValue* request, StringValue* response) override;
  Status getSourceFileName(ServerContext* context, const StringValue* request, StringValue* response) override;
  Status loadObjectNames(ServerContext* context, const RRef* request, StringList* response) override;
  Status findInheritorNamedArguments(ServerContext* context, const RRef* request, StringList* response) override;
  Status findExtraNamedArguments(ServerContext* context, const RRef* request, ExtraNamedArguments* response) override;
  Status getTableColumnsInfo(ServerContext* context, const TableColumnsInfoRequest* request, TableColumnsInfo* response) override;
  Status getFormalArguments(ServerContext* context, const RRef* request, StringList* response) override;
  Status getEqualityObject(ServerContext* context, const RRef* request, Int64Value* response) override;
  Status setValue(ServerContext* context, const SetValueRequest* request, SetValueResponse* response) override;
  Status getObjectSizes(ServerContext* context, const RRefList* request, Int64List* response) override;

  Status getRMarkdownChunkOptions(ServerContext* context, const Empty*, StringList* response) override;

  Status graphicsInit(ServerContext* context, const GraphicsInitRequest* request, ServerWriter<CommandOutput>* writer) override;
  Status graphicsDump(ServerContext* context, const Empty*, ServerWriter<CommandOutput>* writer) override;
  Status graphicsRescale(ServerContext* context, const GraphicsRescaleRequest* request, ServerWriter<CommandOutput>* writer) override;
  Status graphicsRescaleStored(ServerContext* context, const GraphicsRescaleStoredRequest* request, ServerWriter<CommandOutput>* writer) override;
  Status graphicsPullChangedNumbers(ServerContext* context, const Empty*, Int32List* response) override;
  Status graphicsPullSnapshot(ServerContext* context, const GraphicsPullSnapshotRequest* request, GraphicsPullSnapshotResponse* response) override;
  Status graphicsPushSnapshot(ServerContext* context, const GraphicsPushSnapshotRequest* request, google::protobuf::StringValue* response) override;
  Status graphicsCreateGroup(ServerContext* context, const google::protobuf::Empty* request, ServerWriter<CommandOutput>* writer) override;
  Status graphicsRemoveGroup(ServerContext* context, const google::protobuf::StringValue* request, ServerWriter<CommandOutput>* writer) override;
  Status graphicsShutdown(ServerContext* context, const Empty*, ServerWriter<CommandOutput>* writer) override;

  Status httpdRequest(ServerContext* context, const StringValue* request, HttpdResponse* response) override;

  Status beforeChunkExecution(ServerContext *context, const ChunkParameters *request, ServerWriter<CommandOutput> *writer) override;
  Status afterChunkExecution(ServerContext *context, const Empty*, ServerWriter<CommandOutput> *writer) override;
  Status pullChunkOutputRelativePaths(ServerContext *context, const Empty*, StringList* response) override;
  Status pullChunkOutputFile(ServerContext *context, const StringValue* request, PullChunkOutputFileResponse* response) override;

  Status repoGetPackageVersion(ServerContext* context, const StringValue* request, ServerWriter<CommandOutput>* writer) override;
  Status repoInstallPackage(ServerContext* context, const RepoInstallPackageRequest* request, Empty*) override;
  Status repoAddLibraryPath(ServerContext* context, const StringValue* request, ServerWriter<CommandOutput>* writer) override;
  Status repoCheckPackageInstalled(ServerContext* context, const StringValue* request, ServerWriter<CommandOutput>* writer) override;
  Status repoRemovePackage(ServerContext* context, const RepoRemovePackageRequest* request, Empty*) override;

  Status previewDataImport(ServerContext* context, const PreviewDataImportRequest* request, ServerWriter<CommandOutput>* writer) override;
  Status commitDataImport(ServerContext* context, const CommitDataImportRequest* request, Empty*) override;

  Status dataFrameRegister(ServerContext* context, const RRef* request, Int32Value* response) override;
  Status dataFrameGetInfo(ServerContext* context, const RRef* request, DataFrameInfoResponse* response) override;
  Status dataFrameGetData(ServerContext* context, const DataFrameGetDataRequest* request, DataFrameGetDataResponse* response) override;
  Status dataFrameSort(ServerContext* context, const DataFrameSortRequest* request, Int32Value* response) override;
  Status dataFrameFilter(ServerContext* context, const DataFrameFilterRequest* request, Int32Value* response) override;
  Status dataFrameDispose(ServerContext* context, const Int32Value* request, Empty*) override;

  Status getWorkingDir(ServerContext* context, const Empty*, StringValue* response) override;
  Status setWorkingDir(ServerContext* context, const StringValue* request, Empty*) override;
  Status clearEnvironment(ServerContext* context, const RRef* request, Empty*) override;
  Status loadLibrary(ServerContext* context, const StringValue* request, Empty*) override;
  Status unloadLibrary(ServerContext* context, const UnloadLibraryRequest* request, Empty*) override;
  Status saveGlobalEnvironment(ServerContext *context, const StringValue *request, Empty*) override;
  Status loadEnvironment(ServerContext *context, const LoadEnvironmentRequest *request, Empty*) override;
  Status setOutputWidth(ServerContext* context, const Int32Value* request, Empty*) override;
  Status clientRequestFinished(ServerContext* context, const Empty*, Empty*) override;

  Status convertRd2HTML(ServerContext* context, const ConvertRd2HTMLRequest* request,  ServerWriter<CommandOutput> *writer) override;
  Status makeRdFromRoxygen(ServerContext* context, const MakeRdFromRoxygenRequest* request,  ServerWriter<CommandOutput> *writer) override;
  Status findPackagePathByTopic(ServerContext* context, const FindPackagePathByTopicRequest* request,  ServerWriter<CommandOutput> *writer) override;
  Status findPackagePathByPackageName(ServerContext* context, const FindPackagePathByPackageNameRequest* request,  ServerWriter<CommandOutput> *writer) override;
  Status setSaveOnExit(ServerContext* context, const BoolValue* request, Empty*) override;

  void mainLoop();
  std::string readLineHandler(std::string const& prompt);
  void subprocessHandler(
      bool askInput,
      std::function<void(std::string)> const& inputCallback, std::function<void()> const& interruptCallback);
  void subprocessHandlerStop();
  void debugPromptHandler();
  void viewHandler(SEXP x, SEXP title);
  void showFileHandler(std::string const& filePath, std::string const& title);
  void showHelpHandler(std::string const& content, std::string const& url);

  void sendAsyncEvent(AsyncEvent const& e);
  void setChildProcessState();
  volatile bool terminate = false;
  volatile bool terminateProceed = false;

  void executeOnMainThread(std::function<void()> const& f, ServerContext* contextForCancellation = nullptr, bool immediate = false);

  OutputHandler getOutputHandlerForChildProcess();

  void setValueImpl(RRef const& ref, SEXP value);
  SEXP dereference(RRef const& ref);

  OutputHandler replOutputHandler;

private:
  BlockingQueue<AsyncEvent> asyncEvents{8};

  enum ReplState {
    PROMPT, DEBUG_PROMPT, READ_LINE, REPL_BUSY, CHILD_PROCESS, SUBPROCESS_INPUT
  };
  bool isReplOutput = false;
  ReplState replState = REPL_BUSY;
  volatile bool busy = true;
  volatile bool subprocessActive = false;
  bool subprocessInterrupt = false;

  bool isInClientRequest = false;

  std::unordered_set<int> dataFramesCache;
  std::vector<RDebuggerStackFrame> lastErrorStack;

  IndexedStorage<PrSEXP> persistentRefStorage;

  Status executeCommand(ServerContext* context, const std::string& command, ServerWriter<CommandOutput>* writer);

  Status replExecuteCommand(ServerContext* context, const std::string& command);

  friend void quitRPIService();
  friend void saveRWrapperCrashReport(std::string const&);
};

const int CLIENT_RPC_TIMEOUT_MILLIS = 60000;

extern std::unique_ptr<RPIServiceImpl> rpiService;

void initRPIService();
void quitRPIService();

#endif //RWRAPPER_RPI_SERVICE_IMPL_H
