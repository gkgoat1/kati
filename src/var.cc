// Copyright 2015 Google Inc. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// +build ignore

#include "var.h"

#include "eval.h"
#include "expr.h"
#include "log.h"
#include "strutil.h"

unordered_map<const Var*, string> Var::diagnostic_messages_;

const char* GetOriginStr(VarOrigin origin) {
  switch (origin) {
    case VarOrigin::UNDEFINED:
      return "undefined";
    case VarOrigin::DEFAULT:
      return "default";
    case VarOrigin::ENVIRONMENT:
      return "environment";
    case VarOrigin::ENVIRONMENT_OVERRIDE:
      return "environment override";
    case VarOrigin::FILE:
      return "file";
    case VarOrigin::COMMAND_LINE:
      return "command line";
    case VarOrigin::OVERRIDE:
      return "override";
    case VarOrigin::AUTOMATIC:
      return "automatic";
  }
  CHECK(false);
  return "*** broken origin ***";
}

Var::Var() : Var(VarOrigin::UNDEFINED, nullptr, Loc()) {}

Var::Var(VarOrigin origin,  std::shared_ptr<Frame> definition, Loc loc)
    : Evaluable(loc),
      definition_(definition),
      origin_(origin),
      readonly_(false),
      deprecated_(false),
      obsolete_(false),
      self_referential_(false) {}

Var::~Var() {
  diagnostic_messages_.erase(this);
}

void Var::AppendVar(Evaluator*, Value*) {
  CHECK(false);
}

void Var::SetDeprecated(const StringPiece& msg) {
  deprecated_ = true;
  diagnostic_messages_[this] = msg.as_string();
}

void Var::SetObsolete(const StringPiece& msg) {
  obsolete_ = true;
  diagnostic_messages_[this] = msg.as_string();
}

void Var::Used(Evaluator* ev, const Symbol& sym) const {
  if (obsolete_) {
    ev->Error(StringPrintf("*** %s is obsolete%s.", sym.c_str(),
                           diagnostic_message_text()));
  } else if (deprecated_) {
    WARN_LOC(ev->loc(), "%s has been deprecated%s.", sym.c_str(),
             diagnostic_message_text());
  }
}

const char* Var::diagnostic_message_text() const {
  auto it = diagnostic_messages_.find(this);
  return it == diagnostic_messages_.end() ? "" : it->second.c_str();
}

const string& Var::DeprecatedMessage() const {
  static const string empty_string;
  auto it = diagnostic_messages_.find(this);
  return it == diagnostic_messages_.end() ? empty_string : it->second;
}

Var* Var::Undefined() {
  static Var* undefined_var;
  if (!undefined_var) {
    undefined_var = new UndefinedVar();
  }
  return undefined_var;
}

SimpleVar::SimpleVar(VarOrigin origin,  std::shared_ptr<Frame> definition, Loc loc)
    : Var(origin, definition, loc) {}

SimpleVar::SimpleVar(const string& v,
                     VarOrigin origin,
                      std::shared_ptr<Frame> definition,
                     Loc loc)
    : Var(origin, definition, loc), v_(v) {}

SimpleVar::SimpleVar(VarOrigin origin,
                      std::shared_ptr<Frame> definition,
                     Loc loc,
                     Evaluator* ev,
                     Value* v)
    : Var(origin, definition, loc) {
  v->Eval(ev, &v_);
}

bool SimpleVar::IsFunc(Evaluator*) const {
  return false;
}

void SimpleVar::Eval(Evaluator* ev, string* s) const {
  ev->CheckStack();
  *s += v_;
}

void SimpleVar::AppendVar(Evaluator* ev, Value* v) {
  string buf;
  v->Eval(ev, &buf);
  v_.push_back(' ');
  v_ += buf;
  definition_ = ev->CurrentFrame();
}

StringPiece SimpleVar::String() const {
  return v_;
}

string SimpleVar::DebugString() const {
  return v_;
}

RecursiveVar::RecursiveVar(Value* v,
                           VarOrigin origin,
                            std::shared_ptr<Frame> definition,
                           Loc loc,
                           StringPiece orig)
    : Var(origin, definition, loc), v_(v), orig_(orig) {}

bool RecursiveVar::IsFunc(Evaluator* ev) const {
  return v_->IsFunc(ev);
}

void RecursiveVar::Eval(Evaluator* ev, string* s) const {
  ev->CheckStack();
  v_->Eval(ev, s);
}

void RecursiveVar::AppendVar(Evaluator* ev, Value* v) {
  ev->CheckStack();
  v_ = Value::NewExpr(v->Location(), v_, Value::NewLiteral(" "), v);
  definition_ = ev->CurrentFrame();
}

void RecursiveVar::Used(Evaluator* ev, const Symbol& sym) const {
  if (SelfReferential()) {
    ERROR_LOC(
        Location(),
        StringPrintf(
            "*** Recursive variable \"%s\" references itself (eventually).",
            sym.c_str())
            .c_str());
  }

  Var::Used(ev, sym);
}

StringPiece RecursiveVar::String() const {
  return orig_;
}

string RecursiveVar::DebugString() const {
  return Value::DebugString(v_);
}

UndefinedVar::UndefinedVar() {}

bool UndefinedVar::IsFunc(Evaluator*) const {
  return false;
}

void UndefinedVar::Eval(Evaluator*, string*) const {
  // Nothing to do.
}

StringPiece UndefinedVar::String() const {
  return StringPiece("");
}

string UndefinedVar::DebugString() const {
  return "*undefined*";
}

VariableNamesVar::VariableNamesVar(StringPiece name, bool all)
    : name_(name), all_(all) {
  SetReadOnly();
  SetAssignOp(AssignOp::COLON_EQ);
}

bool VariableNamesVar::IsFunc(Evaluator*) const {
  return false;
}

void VariableNamesVar::Eval(Evaluator* ev, string* s) const {
  ConcatVariableNames(ev, s);
}

StringPiece VariableNamesVar::String() const {
  return name_;
}

void VariableNamesVar::ConcatVariableNames(Evaluator* ev, string* s) const {
  WordWriter ww(s);
  vector<StringPiece>&& symbols = GetSymbolNames([=](Var* var) -> bool {
    if (var->Obsolete()) {
      return false;
    }
    if (!all_) {
      if (var->IsFunc(ev)) {
        return false;
      }
    }
    return true;
  });
  for (auto entry : symbols) {
    ww.Write(entry);
  }
}

string VariableNamesVar::DebugString() const {
  return "*VariableNamesVar*";
}

Vars::~Vars() {
  for (auto p : *this) {
    delete p.second;
  }
}

void Vars::add_used_env_vars(Symbol v) {
  used_env_vars_.insert(v);
}

Var* Vars::Lookup(Symbol name) const {
  auto found = find(name);
  if (found == end())
    return Var::Undefined();
  Var* v = found->second;
  if (v->Origin() == VarOrigin::ENVIRONMENT ||
      v->Origin() == VarOrigin::ENVIRONMENT_OVERRIDE) {
    used_env_vars_.insert(name);
  }
  return v;
}

Var* Vars::Peek(Symbol name) const {
  auto found = find(name);
  return found == end() ? Var::Undefined() : found->second;
}

void Vars::Assign(Symbol name, Var* v, bool* readonly) {
  *readonly = false;
  auto p = emplace(name, v);
  if (!p.second) {
    Var* orig = p.first->second;
    if (orig->ReadOnly()) {
      *readonly = true;
      return;
    }
    if (orig->Origin() == VarOrigin::OVERRIDE ||
        orig->Origin() == VarOrigin::ENVIRONMENT_OVERRIDE) {
      return;
    }
    if (orig->Origin() == VarOrigin::AUTOMATIC) {
      ERROR("overriding automatic variable is not implemented yet");
    }
    if (orig->IsDefined())
      delete p.first->second;
    p.first->second = v;
  }
}

SymbolSet Vars::used_env_vars_;

ScopedVar::ScopedVar(Vars* vars, Symbol name, Var* var)
    : vars_(vars), orig_(NULL) {
  auto p = vars->emplace(name, var);
  iter_ = p.first;
  if (!p.second) {
    orig_ = iter_->second;
    iter_->second = var;
  }
}

ScopedVar::~ScopedVar() {
  if (orig_) {
    iter_->second = orig_;
  } else {
    vars_->erase(iter_);
  }
}
