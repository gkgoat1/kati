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

#ifndef VAR_H_
#define VAR_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "eval.h"
#include "expr.h"
#include "loc.h"
#include "log.h"
#include "stmt.h"
#include "string_piece.h"
#include "symtab.h"

using namespace std;

class Evaluator;
class Value;

enum struct VarOrigin : char {
  UNDEFINED,
  DEFAULT,
  ENVIRONMENT,
  ENVIRONMENT_OVERRIDE,
  FILE,
  COMMAND_LINE,
  OVERRIDE,
  AUTOMATIC,
};

const char* GetOriginStr(VarOrigin origin);

class Var : public Evaluable {
 public:
  virtual ~Var();

  virtual const char* Flavor() const = 0;

  VarOrigin Origin() const { return origin_; }
   std::shared_ptr<Frame> Definition() const { return definition_; }

  virtual bool IsDefined() const { return true; }

  virtual void AppendVar(Evaluator* ev, Value* v);

  virtual StringPiece String() const = 0;

  virtual string DebugString() const = 0;

  bool ReadOnly() const { return readonly_; }
  void SetReadOnly() { readonly_ = true; }

  bool Deprecated() const { return deprecated_; }
  void SetDeprecated(const StringPiece& msg);

  bool Obsolete() const { return obsolete_; }
  void SetObsolete(const StringPiece& msg);

  bool SelfReferential() const { return self_referential_; }
  void SetSelfReferential() { self_referential_ = true; }

  const string& DeprecatedMessage() const;

  // This variable was used (either written or read from)
  virtual void Used(Evaluator* ev, const Symbol& sym) const;

  AssignOp op() const { return assign_op_; }
  void SetAssignOp(AssignOp op) { assign_op_ = op; }

  static Var* Undefined();

 protected:
  Var();
  Var(VarOrigin origin,  std::shared_ptr<Frame> definition, Loc loc);

   std::shared_ptr<Frame> definition_;

 private:
  const VarOrigin origin_;

  AssignOp assign_op_;
  bool readonly_ : 1;
  bool deprecated_ : 1;
  bool obsolete_ : 1;
  bool self_referential_ : 1;

  const char* diagnostic_message_text() const;

  static unordered_map<const Var*, string> diagnostic_messages_;
};

class SimpleVar : public Var {
 public:
  explicit SimpleVar(VarOrigin origin,  std::shared_ptr<Frame> definition, Loc loc);
  SimpleVar(const string& v, VarOrigin origin,  std::shared_ptr<Frame> definition, Loc loc);
  SimpleVar(VarOrigin origin,
             std::shared_ptr<Frame> definition,
            Loc loc,
            Evaluator* ev,
            Value* v);

  virtual const char* Flavor() const override { return "simple"; }

  virtual bool IsFunc(Evaluator* ev) const override;

  virtual void Eval(Evaluator* ev, string* s) const override;

  virtual void AppendVar(Evaluator* ev, Value* v) override;

  virtual StringPiece String() const override;

  virtual string DebugString() const override;

  string v_;
};

class RecursiveVar : public Var {
 public:
  RecursiveVar(Value* v,
               VarOrigin origin,
                std::shared_ptr<Frame> definition,
               Loc loc,
               StringPiece orig);

  virtual const char* Flavor() const override { return "recursive"; }

  virtual bool IsFunc(Evaluator* ev) const override;

  virtual void Eval(Evaluator* ev, string* s) const override;

  virtual void AppendVar(Evaluator* ev, Value* v) override;

  virtual StringPiece String() const override;

  virtual string DebugString() const override;

  virtual void Used(Evaluator* ev, const Symbol& sym) const override;

  Value* v_;
  StringPiece orig_;
};

class UndefinedVar : public Var {
 public:
  UndefinedVar();

  virtual const char* Flavor() const override { return "undefined"; }
  virtual bool IsDefined() const override { return false; }

  virtual bool IsFunc(Evaluator* ev) const override;

  virtual void Eval(Evaluator* ev, string* s) const override;

  virtual StringPiece String() const override;

  virtual string DebugString() const override;
};

// The built-in VARIABLES and KATI_SYMBOLS variables
class VariableNamesVar : public Var {
 public:
  VariableNamesVar(StringPiece name, bool all);

  virtual const char* Flavor() const override { return "kati_variable_names"; }
  virtual bool IsDefined() const override { return true; }

  virtual bool IsFunc(Evaluator* ev) const override;

  virtual void Eval(Evaluator* ev, string* s) const override;

  virtual StringPiece String() const override;

  virtual string DebugString() const override;

 private:
  StringPiece name_;
  bool all_;

  void ConcatVariableNames(Evaluator* ev, string* s) const;
};

class Vars : public unordered_map<Symbol, Var*> {
 public:
  ~Vars();

  Var* Lookup(Symbol name) const;
  Var* Peek(Symbol name) const;

  void Assign(Symbol name, Var* v, bool* readonly);

  static void add_used_env_vars(Symbol v);

  static const SymbolSet used_env_vars() { return used_env_vars_; }

 private:
  static SymbolSet used_env_vars_;
};

class ScopedVar {
 public:
  // Does not take ownerships of arguments.
  ScopedVar(Vars* vars, Symbol name, Var* var);
  ~ScopedVar();

 private:
  Vars* vars_;
  Var* orig_;
  Vars::iterator iter_;
};

#endif  // VAR_H_
