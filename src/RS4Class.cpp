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
#include "RStuff/RObjects.h"

Status RPIServiceImpl::getLoadedShortS4ClassInfos(ServerContext* context, const Empty*, ShortS4ClassInfoList* response) {
  executeOnMainThread([&] {
    ShieldSEXP jetbrainsEnv = RI->globalEnv.getVar(".jetbrains");
    ShieldSEXP func = jetbrainsEnv.getVar("getLoadedS4ClassInfos");
    ShieldSEXP result = func();
    if (TYPEOF(result) != VECSXP) return;

    for (int i = 0; i < result.length(); ++i) {
      ShieldSEXP classRep = VECTOR_ELT(result, i);
      if (TYPEOF(classRep) != S4SXP) continue;

      ShortS4ClassInfoList_ShortS4ClassInfo* info = response->add_shorts4classinfos();
      info->set_name(stringEltUTF8(R_do_slot(classRep, toSEXP("className")), 0));
      info->set_package(stringEltUTF8(R_do_slot(classRep, toSEXP("package")), 0));
      info->set_isvirtual(asBool(R_do_slot(classRep, toSEXP("virtual"))));
    }
  }, context, true);
  return Status::OK;
}

struct SlotInfo {
  string name, type, declarationClass;
};

std::vector<SlotInfo> extractSlots(const ShieldSEXP &classDef) {
  std::unordered_map<string, SlotInfo> slotsInfos;
  auto defProcessor = [&slotsInfos](const ShieldSEXP &classDef) {
    if (TYPEOF(classDef) != S4SXP) return;
    auto className = stringEltUTF8(R_do_slot(classDef, toSEXP("className")), 0);
    ShieldSEXP slotsList = R_do_slot(classDef, toSEXP("slots"));
    ShieldSEXP slotsNames = Rf_getAttrib(slotsList, R_NamesSymbol);
    for (int i = 0; i < slotsNames.length(); ++i) {
      auto slotName = stringEltUTF8(slotsNames, i);
      auto slotType = stringEltUTF8(VECTOR_ELT(slotsList, i), 0);
      if (slotsInfos.count(slotName) == 0 ||
          R_extends(toSEXP(slotType), toSEXP(slotsInfos[slotName].type), RI->globalEnv) == TRUE) {
        slotsInfos[slotName] = {slotName, slotType, className};
      }
    }
  };

  ShieldSEXP superClassesList = R_do_slot(classDef, toSEXP("contains"));
  std::vector<std::pair<int, string>> superClasses;
  for (int i = 0; i < superClassesList.length(); ++i) {
    ShieldSEXP superClass = VECTOR_ELT(superClassesList, i);
    auto superClassName = stringEltUTF8(R_do_slot(superClass, toSEXP("superClass")), 0);
    auto distance = asInt(R_do_slot(superClass, toSEXP("distance")));
    superClasses.emplace_back(distance, superClassName);
  }
  std::sort(superClasses.begin(), superClasses.end());

  defProcessor(classDef);
  for (auto& e : superClasses) {
    defProcessor(R_getClassDef(e.second.c_str()));
  }

  std::vector<SlotInfo> result;
  result.reserve(slotsInfos.size());
  for (auto &e : slotsInfos) {
    result.push_back(e.second);
  }
  return result;
}

void getS4ClassInfo(const ShieldSEXP &classDef, S4ClassInfo *response) {
  if (TYPEOF(classDef) != S4SXP) return;
  response->set_classname(stringEltUTF8(R_do_slot(classDef, toSEXP("className")), 0));
  response->set_packagename(stringEltUTF8(R_do_slot(classDef, toSEXP("package")), 0));
  for (auto& slot : extractSlots(classDef)) {
    auto nextSlot = response->add_slots();
    nextSlot->set_name(slot.name);
    nextSlot->set_type(slot.type);
    nextSlot->set_declarationclass(slot.declarationClass);
  }

  ShieldSEXP superClasses = R_do_slot(classDef, toSEXP("contains"));
  for (int i = 0; i < superClasses.length(); ++i) {
    ShieldSEXP superClass = VECTOR_ELT(superClasses, i);
    auto nextSuperClass = response->add_superclasses();
    nextSuperClass->set_name(stringEltUTF8(R_do_slot(superClass, toSEXP("superClass")), 0));
    nextSuperClass->set_distance(asInt(R_do_slot(superClass, toSEXP("distance"))));
  }

  response->set_isvirtual(asBool(R_do_slot(classDef, toSEXP("virtual"))));
}

Status RPIServiceImpl::getS4ClassInfoByObjectName(ServerContext* context, const RRef* request, S4ClassInfo* response) {
  executeOnMainThread([&] {
    ShieldSEXP obj = dereference(*request);
    if (TYPEOF(obj) != S4SXP) return;
    ShieldSEXP className = Rf_getAttrib(obj, R_ClassSymbol);
    getS4ClassInfo(R_getClassDef_R(className), response);
  }, context, true);
  return Status::OK;
}

Status RPIServiceImpl::getS4ClassInfoByClassName(ServerContext* context, const StringValue* request, S4ClassInfo* response) {
  executeOnMainThread([&] {
    getS4ClassInfo(R_getClassDef(request->value().c_str()), response);
  }, context, true);
  return Status::OK;
}