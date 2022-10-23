#include "InfoFlow/Query.h"
#include <sstream>

std::map<const Instruction*, std::string> instStrMap;

extern const Instruction *getNodeInstruction(SVFGNode *node);

extern std::string prettyPrint(SVFGNode *node);

std::string getTypeName(const Type *t) {
   std::string type_str;
   llvm::raw_string_ostream rso(type_str);
   t->print(rso);
   std::istringstream iss(rso.str());
   std::string typestr;
   getline(iss, typestr, ' ');
   std::string structpref("%struct.");
   int pos = typestr.find(structpref);
   if (pos == 0)
      typestr = typestr.substr(structpref.size());
   return typestr;
}

std::string getFunctionName(std::string str) {
   std::string preamble = "@";
   int pos_pre = str.find(preamble);
   int pos_par = str.find("(");
   if (pos_pre != std::string::npos && pos_par != std::string::npos && 
       str.length() > pos_pre + 1 && pos_par - pos_pre > 0) {
      std::string fname = str.substr(pos_pre + 1, pos_par - pos_pre);       
      return fname;
   }
   else return str;
}

std::string getInstructionStr(const llvm::Instruction *inst) {
   if (instStrMap.find(inst) != instStrMap.end())
      return instStrMap[inst];
   std::string str;
   llvm::raw_string_ostream rso(str);
   rso << (*inst) << "\n";
   instStrMap[inst] = rso.str();
   return rso.str();
}

extern bool isFunctionPointer(const llvm::Type *t);

Identifier::Identifier() { this->entitytype = noneetype; }

Identifier::Identifier(ETYPE entitytype, std::string name) {
  this->entitytype = entitytype;
  this->name = name;
  this->tname = "";
  this->field = -1; 
  this->argNo = -1;
}


Identifier::Identifier(ETYPE entitytype, std::string name, int argNo) {
  this->entitytype = entitytype;
  this->name = name;
  this->tname = "";
  this->field = -1; 
  this->argNo = argNo;
}

Identifier::Identifier(ETYPE entitytype, std::string name, std::string tname, int field) {
  this->entitytype = entitytype;
  this->name = name;
  this->tname = tname;
  this->field = field;
  this->argNo = -1; 
}

Identifier::Identifier(const Identifier &i) {
  this->name = i.name;
  this->entitytype = i.entitytype;
  this->tname = i.tname;
  this->field = i.field; 
}

QueryExp::QueryExp() { this->type = noneqtype; }

QueryExp::QueryExp(QTYPE type) {
   this->type = type;
}

RegExpQueryExp::RegExpQueryExp() {
   this->type = regexp;
   equals = "";
   beginsWith = "";
   endsWith = "";
}

RegExpQueryExp::RegExpQueryExp(const RegExpQueryExp &r) {
  this->type = r.type;
  this->equals = r.equals;
  this->beginsWith = r.beginsWith;
  this->endsWith = r.endsWith;
  this->doesnotendWith = r.doesnotendWith;
  this->doesnotbeginWith = r.doesnotbeginWith;
  this->includes = r.includes;
  this->excludes = r.excludes;
  this->orIncludes = r.orIncludes;
}

RegExpQueryExp::RegExpQueryExp(std::string idname, ETYPE entitytype) {
  this->type = regexp;
  id = new Identifier(entitytype, idname);
  llvm::errs() << " in RegExp " << id->getName() << " type " << entitytype << "\n";
  equals = "";
  beginsWith = "";
  endsWith = "";
}

RegExpQueryExp::RegExpQueryExp(std::string idname, ETYPE entitytype, int arg) {
  this->type = regexp;
  id = new Identifier(entitytype, idname, arg);
  llvm::errs() << " in RegExp " << id->getName() << " type " << entitytype << "\n";
  equals = "";
  beginsWith = "";
  endsWith = "";
}


RegExpQueryExp::RegExpQueryExp(std::string idname, ETYPE entitytype, std::string tname, int field) {
  this->type = regexp;
  id = new Identifier(entitytype, idname, tname, field);
  llvm::errs() << " in RegExp " << id->getName() << " type " << entitytype << "\n";
  equals = "";
  beginsWith = "";
  endsWith = "";
}

QueryExp *RegExpQueryExp::reverse() {
   RegExpQueryExp *res = new RegExpQueryExp(*this);
   return res;
}

void RegExpQueryExp::setEndsWith(std::string str) {
   this->endsWith = str;
}
    
void RegExpQueryExp::addDoesNotEndWith(std::string str) {
   doesnotendWith.insert(str);
}

void RegExpQueryExp::setBeginsWith(std::string str) {
   this->beginsWith = beginsWith;
}

void RegExpQueryExp::addDoesNotBeginWith(std::string str) {
   doesnotbeginWith.insert(str);
}

void RegExpQueryExp::addIncludes(std::string str) {
   includes.insert(str);
}

void RegExpQueryExp::addOrIncludes(std::string str) {
   orIncludes.insert(str);
}

void RegExpQueryExp::addExcludes(std::string str) {
   excludes.insert(str);
}

void RegExpQueryExp::setEquals(std::string str) {
  equals = str;
}

bool beginsWithStr(std::string str, std::string bstr) {
   if (str.size() < bstr.size())
      return false;
   for(int i=0; i<bstr.size() && i<str.size(); i++) {
      if (bstr[i] != str[i])
         return false;
   }  
   return true; 
}
bool endsWithStr(std::string str, std::string estr) {
  if (str.size() < estr.size())
      return false;
  for(int i=estr.size()-1, j=str.size()-1; i>=0 && j>=0; i--, j--) {
     if (estr[i] != str[j])
        return false;
  }
  return true;
}

bool RegExpQueryExp::matches(std::string str) {
  
  if (equals != "") {
     if (str == equals) {
        //llvm::errs() << "String " << str << " matches equals " << equals << "\n"; 
        return true;
     }
     else {
        //llvm::errs() << "String " << str << " does not match equals " << equals << "\n"; 
        return false;
     }
  }
  if (beginsWith != "") {
     if (!beginsWithStr(str, beginsWith)) {
         //llvm::errs() << "String " << str << " does not match begins with " << beginsWith << "\n"; 
         return false;
     }
     //else 
       //  llvm::errs() << "String " << str << " matches begins with " << beginsWith << "\n"; 
  }
  for(auto db : doesnotbeginWith) {
     if (beginsWithStr(str, db)) {
        //llvm::errs() << "String " << str << " does not match by matching does not begin with " << db << "\n"; 
        return false; 
     }
     //else 
       // llvm::errs() << "String " << str << " matches by not matching does not begin with " << db << "\n"; 
  }
  if (endsWith != "") {
     if (!endsWithStr(str, endsWith)) {
        //llvm::errs() << "String " << str << " does not match ends with " << endsWith << "\n"; 
        return false;
     }
     //else 
       // llvm::errs() << "String " << str << " matches end with " << endsWith << "\n"; 
  }
  for(auto de : doesnotendWith) {
     if (endsWithStr(str, de)) {
        //llvm::errs() << "String " << str << " does not match by not matching does not end with " << de << "\n"; 
        return false;
     }
     //else
       // llvm::errs() << "String " << str << " matches by not matching does not end with " << de << "\n"; 
  }
  for(auto inc : includes) {
     if (str.find(inc) == std::string::npos) {
        //llvm::errs() << "String " << str << " does not match by not matching includes " << inc << "\n";
        return false; 
     }
     //else 
       //  llvm::errs() << "String " << str << " matches by including " << inc << "\n";
  }
  if (orIncludes.size() > 0) {
     bool found = false;
     for(auto inc : orIncludes) {
        //llvm::errs() << "orIncludes element=" << inc << "\n";
        if (str.find(inc) != std::string::npos) {
           //llvm::errs() << "String " << str << " matches by including " << inc << "\n";
           found = true;
           break;
        }
     }
     if (!found)
         return false;
  }
  for(auto exc : excludes) {
     if (str.find(exc) != std::string::npos) {
        //llvm::errs() << "String " << str << " does not match by not matching excludes " << exc << "\n"; 
        return false; 
     }
     //else  
       //  llvm::errs() << "String " << str << " matches by excluding " << exc << "\n";
  }
  return true;
}


RelationalQueryExp::RelationalQueryExp() {
  this->type = relationalexp;
}

RelationalQueryExp::RelationalQueryExp(Identifier *id1, Identifier *id2, UTYPE utype) {
  this->id1 = id1;
  this->id2 = id2; 
  this->type = relationalexp;
  this->utype = utype;
  this->id3 = NULL;
}


RelationalQueryExp::RelationalQueryExp(Identifier *id1, Identifier *id2, Identifier *id3, UTYPE utype) {
  this->id1 = id1;
  this->id2 = id2; 
  this->id3 = id3; 
  this->type = relationalexp;
  this->utype = utype;
}


RelationalQueryExp::RelationalQueryExp(const RelationalQueryExp &r) {
   this->id1 = new Identifier(*r.id1);
   this->id2 = new Identifier(*r.id2);
   if (r.id3)
      this->id3 = new Identifier(*r.id3);
   else this->id3 = NULL;
   this->type = relationalexp;
   this->utype = r.utype;
}

QueryExp *RelationalQueryExp::reverse() {
  if (id3)
     return NULL;
  RelationalQueryExp *res = new RelationalQueryExp();
  res->id1 = new Identifier(*id2);
  res->id2 = new Identifier(*id1);
  res->type = relationalexp;
  if (this->utype == calls)
     res->utype = calledby;
  else if (this->utype == calledby)
     res->utype = calls;
  else if (this->utype == taints)
     res->utype = taintedby;
  else if (this->utype == taintedby)
     res->utype = taints;
  else if (this->utype == formalparameterpasses)
     res->utype = formalparameterpassedby;
  else if (this->utype == formalparameterpassedby)
     res->utype == formalparameterpasses;
  else if (this->utype == actualparameterpasses)
     res->utype = actualparameterpassedby;
  else if (this->utype == actualparameterpassedby)
     res->utype == actualparameterpasses;
  else if (this->utype == callbackdefinedin)
     res->utype = definescallback;
  else if (this->utype == definescallback)
     res->utype =  callbackdefinedin;
  else if (this->utype == fieldof)
     res->utype = hasfield;
  else if (this->utype == hasfield)
     res->utype == fieldof;
  else assert(0 && "Reverse of query for an invalid type!\n");
  return res;
}

Query::Query() {
  exp = NULL;
  sanitizationConstraint = NULL;
  //sanitizationReturnValue = 0;
}

Query::Query(QueryExp *qexp) {
  this->exp = qexp;
  sanitizationConstraint = NULL;
  //sanitizationReturnValue = 0;
}

void Query::addConstraint(std::string id, Constraint *cons) {
   this->constraints[id] = cons;
}

void Query::addConstraint(Constraint *cons) {
   Identifier *id = cons->getIdentifier();
   if (id) {
      std::string s = id->getName();
      //llvm::errs() << " id name " << s << "\n";
      this->constraints[s] = cons;
   }
}

void Query::setSanitizationConstraint(Constraint *cons, int returnvalue) {
  sanitizationConstraint = cons;
  sanitizationReturnValue.push_back(returnvalue);
}

void Query::setSanitizationConstraint(Constraint *cons, int returnvalue, int returnvalue2, int returnvalue3) {
  sanitizationConstraint = cons;
  sanitizationReturnValue.push_back(returnvalue);
  sanitizationReturnValue.push_back(returnvalue2);
  sanitizationReturnValue.push_back(returnvalue3);
}

Constraint *Query::getSanitizationConstraint() {
   return sanitizationConstraint;
}


Query *Query::reverse() {
   Query *res = new Query();
   if (exp)
      res->exp = exp->reverse();
   else res->exp = NULL;
   res->constraints = constraints;
   return res;
}

Constraint::Constraint() {
  id = NULL;
  label = NULL;
}
 
Constraint::Constraint(Identifier *id, Label *label) {
  this->id = id;
  this->label = label;
}

Label::Label() {
   desc = "";
   query = NULL;
   solution = NULL;
   isnegated = false;
   isatomic = true; 
}

Label::Label(const Label &label) {
   this->desc = label.desc;
   this->query = label.query;
   this->solution = label.solution;
   this->isnegated = label.isnegated;
   this->isatomic = label.isatomic;
}

Label::Label(std::string desc, Query *query) {
   this->desc = desc;
   this->query = query;
   solution = NULL;
   isnegated = false;
   isatomic = true;  
}

void Label::setSolution(Solution *sol) {
   solution = sol;
}

Solution *Label::getSolution() {
  return solution;
} 



bool Label::hasSolution() {
  return (solution != NULL);
}

bool Label::isAtomic() {
  return isatomic;
}

bool Label::isAMember(Entity *entity) {
   assert(solution != NULL && "Should not query the label if solution has not been computed yet!\n");
   return solution->isAMember(entity);
} 


// change the direction of the use relationship like transpose
Label *Label::reverse(std::string newdesc) {
   Label *res = new Label(*this);
   res->desc = newdesc;
   res->query = query->reverse();
   res->solution = NULL;
   if (solution) {
      if (solution->isBinary()) 
         res->solution = ((BinarySolution*)solution)->reverse();
      res->solution->setLabel(res);
   }
   return res;
}

// negate the relationship
void Label::setNegated(bool neg) {
   isnegated = true;
}

bool Label::isNegated() {
   return isnegated;
}

bool Label::isRelational() {
   return (query->getQueryExp()->getType() == relationalexp);
}

QueryExp *Label::getQueryExp() {
   return query->getQueryExp();
}

CompositeLabel::CompositeLabel() {
  composetype = nonectype;
}

CompositeLabel::CompositeLabel(std::string desc, CTYPE composetype) : Label() {
   this->desc = desc;
   this->composetype = composetype;
   this->isatomic = false;
}
 
void CompositeLabel::addLabel(Label *label) {
   labels.push_back(label);
}

Entity::Entity() {
}

Entity::Entity(ETYPE type, SVFGNode *node, const llvm::Type *ltype, int index, const llvm::Value *value, const llvm::CallSite *cs) {
   this->type = type; 
   this->node = node;
   this->value = value;
   this->ltype = ltype;
   this->findex = index;
   this->callsite = cs;
}

bool Entity::definesCallback(Entity *e1, Entity *e2) {
   if (e1->getType() != structT)
      return false;
   if (e2->getType() != funcptrfieldT)
      return false;
   DataEntity *de = (DataEntity*)e1;
   FuncPtrFieldEntity *fe = (FuncPtrFieldEntity*)e2;
   return (getTypeName(de->getLType()) == getTypeName(fe->getLType()));
}


bool usesOf(const Instruction *i1, const Instruction *i2, const BasicBlock *bb) {
   //llvm::errs() << "Does " << (*i1) << " use " << (*i2) << "?\n";
   if (i2->getParent() != bb) return false;
   //llvm::errs() << "Uses:\n";
   for(const Use &U : i2->operands())  {
   //for(Value::const_use_iterator ui = i2->use_begin(); ui != i2->use_end(); ui++) { 
      //llvm::errs() << "\t" << (*U) << "\n";
      if (Instruction *ii = llvm::dyn_cast<Instruction>(U)) {
         if (ii == i2) continue;
         if (ii->getOpcode() == Instruction::Load) // approximating
            return true;
         if (ii == i1)
            return true;
         else if (usesOf(i1, ii, bb))
            return true;
     }
   }
   return false;
}

/*GetElementPtrInst *getGep(Instruction *ii, BasicBlock *bb) {
   llvm::errs() << "Searching gep, current " << (*ii) << "\n";
   if (ii->getParent() != bb) return NULL;
   for(Value::use_iterator ui = ii->use_begin(); ui != ii->use_end(); ui++) {
      llvm::errs() << "\t" << (*(*ui)) << "\n";
      if (*ui == ii) continue;
      if (GetElementPtrInst *ii2 = llvm::dyn_cast<GetElementPtrInst>(*ui)) {
          return ii2;
      }
      else {
          if (Instruction *ii3 = llvm::dyn_cast<Instruction>(*ui)) {
             GetElementPtrInst *gi = getGep(ii3, bb);
             if (gi) return gi;
          }
      }
   }
   return NULL;
}*/


std::set<Type*> getStructTypeCandidates(CallSite cs) {
  std::set<Type*> res;
  Instruction *inst = cs.getInstruction();
  //CallInst *cinst = llvm::dyn_cast<CallInst>(inst);
  //if (!cinst) return res;
  //Instruction *finst = llvm::dyn_cast<Instruction>(cinst->getCalledOperand());
  //if (!finst) return res;
  BasicBlock *bb = inst->getParent();
  
  for(BasicBlock::iterator it = bb->begin(); it != bb->end(); it++) {
    Instruction *ii = &(*it);
    if (ii->getOpcode() == Instruction::GetElementPtr) {
       GetElementPtrInst *gep = llvm::dyn_cast<GetElementPtrInst>(ii);
       if (usesOf(gep, inst, bb)) {      
        if (gep) {      
           Value *pv = gep->getPointerOperand();
           Type *t = pv->getType();
           if (t->isPointerTy())
              t = t->getPointerElementType();
           if (t->isStructTy())
              res.insert(t);
        }
      }
    }
        
  }
  return res;
}

bool Entity::indirectCallOf(Entity *e1, Entity *e2) {
  if (e1->getType() != callsiteT)
     return false;
  if (e2->getType() != funcptrfieldT)
      return false;
  CallsiteEntity *cs = (CallsiteEntity*)e1;
  //if (cs->toString() != "")
  //   return false;
  FuncPtrFieldEntity *fe = (FuncPtrFieldEntity*)e2;
  std::set<Type*> cts = getStructTypeCandidates(*cs->getCallsite());
  if (cts.empty()) 
     return false;
  llvm::errs() << "Candidate struct types: \n";
  bool found = false;
  for(auto ct : cts) {
     llvm::errs() << "Type " << getTypeName(ct) << "\n";
     if (getTypeName(ct) == getTypeName(fe->getLType())) {
        found = true;
        break;
     }
  }
  if (!found) return false;
  if (cs->getNumArgs() != fe->getNumArgs())
     return false;
  for(int i=0; i<cs->getNumArgs(); i++) { 
     //llvm::errs() << "Comparing " << getTypeName(cs->getArg(i)->getType()) 
       //           << " vs " << getTypeName(fe->getArgType(i)) << "\n";
     if (getTypeName(cs->getArg(i)->getType()) != getTypeName(fe->getArgType(i)))
        return false; 
  }
  llvm::errs() << "Indirectcallof works for : " << e1->toString() << " and " << e2->toString() << "\n";
  return true;
}

bool Entity::actualparameterpassedby(Entity *e1, Entity *e2) {
   if (!((e1->getType() == primitiveFuncArgT) || 
         (e1->getType() == pointerFuncArgT) ||
         (e1->getType() == funcptrFuncArgT) ||
         (e1->getType() == structFuncArgT)))
      return false; 
   if (e2->getType() != callsiteT)
      return false;
   FunctionArgEntity *fe = (FunctionArgEntity*)e1;
   CallsiteEntity *cs = (CallsiteEntity*)e2;
   if (ActualParmSVFGNode *ap = SVFUtil::dyn_cast<ActualParmSVFGNode>(fe->node)) {
      CallSite acs = ap->getCallSite();
      std::string str1 = getInstructionStr(acs.getInstruction());
      std::string str2 = getInstructionStr(e2->callsite->getInstruction());
      //llvm::errs() << str1 << " vs " << str2 << "\n";
      return (str1 == str2);
   }
   bool ain = SVFUtil::isa<ActualINSVFGNode>(e1->node);
   bool aout = SVFUtil::isa<ActualOUTSVFGNode>(e1->node);
   if (!ain && !aout) 
      return false;
   //llvm::errs() << "In actualparpass first entity an actual in or out\n";
   if (ain) {
      ActualINSVFGNode *ainnode = SVFUtil::dyn_cast<ActualINSVFGNode>(e1->node);
      assert(ainnode);
      CallSite acs = ainnode->getCallSite();
      std::string str1 = getInstructionStr(acs.getInstruction());
      std::string str2 = getInstructionStr(e2->callsite->getInstruction());
      //llvm::errs() << str1 << " vs " << str2 << "\n";
      return (str1 == str2);
   }
   if (aout) {
      ActualOUTSVFGNode *aoutnode = SVFUtil::dyn_cast<ActualOUTSVFGNode>(e1->node);
      assert(aoutnode);
      CallSite acs = aoutnode->getCallSite();
      std::string str1 = getInstructionStr(acs.getInstruction());
      std::string str2 = getInstructionStr(e2->callsite->getInstruction());
      //llvm::errs() << str1 << " vs " << str2 << "\n";
      return (str1 == str2);
   }
   return false;
}

bool Entity::taints(Entity *e1, Entity *e2) {
   SVFGNode *s1 = e1->node;
   SVFGNode *s2 = e2->node;
  
   //if (SVFUtil::isa<>(s1) 
}

Entity *Entity::create(ETYPE t, SVFGNode *n) {
  if (t == addrExprT)
     return new AddrExpressionEntity(n);
  else if (t == fieldAddrExprT)
     return new FieldAddrExpressionEntity(n);
  else if (t == genericValueT)
     return new GenericValueEntity(n);
  // to do: other Entity types that rely on an svfgnode 
  return NULL;
}

std::string getClusterName(std::string fname) {
   std::string clustername = fname;
   int pos = fname.find("_");
   if (pos != std::string::npos)
      clustername = fname.substr(0,pos);
   else if (fname.size() > 3)
      clustername = fname.substr(0,3);
   return clustername;
}

std::map<std::string, std::set<Entity*>*> Entity::generateClusters(std::set<Entity*> eset) {
  std::map<std::string, std::set<Entity*>*> cl;
  std::string temp = std::string("unknown");
  for(auto e : eset) {
     if (e->getType() == funcArgT || e->getType() == primitiveFuncArgT || 
         e->getType() == pointerFuncArgT ||
         e->getType() == funcptrFuncArgT || e->getType() == structFuncArgT) {
        const Function *f = NULL;
        if (e->getValue() && (f = llvm::dyn_cast<Function>(e->getValue()))) {
           std::string clustername = getClusterName(f->getName());
           if (cl.find(clustername) == cl.end())
              cl[clustername] = new std::set<Entity*>();
           cl[clustername]->insert(e);
        }
        else  {
           if (cl.find(temp) == cl.end())
              cl[temp] = new std::set<Entity*>();
           cl[temp]->insert(e);
        }
     }
     else if (e->getType() == callsiteT) {
        if (const Function *f = ((CallsiteEntity*)e)->getFunctionContext()) {
          std::string clustername = getClusterName(f->getName());
          if (cl.find(clustername) == cl.end())
              cl[clustername] = new std::set<Entity*>();
           cl[clustername]->insert(e);
        }
        else  {
           std::string temp = std::string("unknown");
           if (cl.find(temp) == cl.end())
              cl[temp] = new std::set<Entity*>();
           cl[temp]->insert(e);
        }
     }
     else {
        if (cl.find(temp) == cl.end())
           cl[temp] = new std::set<Entity*>();
        cl[temp]->insert(e);
     }
  }
  return cl;
}


GenericValueEntity::GenericValueEntity() : Entity(genericValueT, NULL, NULL, -1, NULL, NULL) {
}

GenericValueEntity::GenericValueEntity(const GenericValueEntity &ge) {
  type = genericValueT;
  node = ge.node;
  ltype = ge.ltype;
  value = ge.value;
  callsite = ge.callsite;
  findex = ge.findex;
}

GenericValueEntity::GenericValueEntity(SVFGNode *n) : Entity(genericValueT, n, NULL, -1, NULL, NULL) {
}

bool GenericValueEntity::matches(Entity *e) {
  if (!e) return false;
  return (node && e->getNode() && node->getId() == e->getNode()->getId());
}

std::string GenericValueEntity::getStringRep() {
   if (!node) return "Generic value ";
   return "Generic value with node " + std::to_string(node->getId()) + " " + prettyPrint(node);
}

std::string GenericValueEntity::toString() {
  return getStringRep();
}

AddrExpressionEntity::AddrExpressionEntity() {
  this->type = addrExprT;
  this->node = NULL;
  this->ltype = NULL;
  this->value = NULL;
  this->callsite = NULL;
  this->findex = -1;
}

AddrExpressionEntity::AddrExpressionEntity(const AddrExpressionEntity &ae) {
  this->type = addrExprT;
  this->node = ae.node;
  this->ltype = ae.ltype;
  this->value = ae.value;
  this->callsite = ae.callsite;
  this->findex = ae.findex;
}

AddrExpressionEntity::AddrExpressionEntity(SVFGNode *node) {
  this->type = addrExprT;
  this->node = node;
  this->ltype = NULL;
  this->value = NULL;
  this->callsite = NULL;
  this->findex = -1;
}

bool AddrExpressionEntity::matches(Entity *ae) {
  return (type == ae->getType() && node && ae->getNode() && node->getId() == ae->getNode()->getId());
}

std::string AddrExpressionEntity::getStringRep() {
  if (node) {
     const Instruction *inst = getNodeInstruction(node);
     if (inst) {
        return "Inside " + inst->getParent()->getParent()->getName().str() + " " + getInstructionStr(inst);
     }
  }
  else return "Address exp entity\n";
}

std::string AddrExpressionEntity::toString() {
  return getStringRep();
}


FieldAddrExpressionEntity::FieldAddrExpressionEntity() {
  this->type = fieldAddrExprT;
  this->node = NULL;
  this->ltype = NULL;
  this->value = NULL;
  this->callsite = NULL;
  this->findex = -1;
}

FieldAddrExpressionEntity::FieldAddrExpressionEntity(const FieldAddrExpressionEntity &ae) {
  this->type = fieldAddrExprT;
  this->node = ae.node;
  this->ltype = ae.ltype;
  this->value = ae.value;
  this->callsite = ae.callsite;
  this->findex = ae.findex;
}

FieldAddrExpressionEntity::FieldAddrExpressionEntity(SVFGNode *node) {
  this->type = fieldAddrExprT;
  this->node = node;
  this->ltype = NULL;
  this->value = NULL;
  this->callsite = NULL;
  this->findex = -1;
}

bool FieldAddrExpressionEntity::matches(Entity *ae) {
  return (type == ae->getType() && node && ae->getNode() && node->getId() == ae->getNode()->getId());
}

std::string FieldAddrExpressionEntity::toString() {
  return getStringRep();
}

std::string FieldAddrExpressionEntity::getStringRep() {
  if (node) {
     const Instruction *inst = getNodeInstruction(node);
     if (inst) {
        return "Inside " + inst->getParent()->getParent()->getName().str() + " " + getInstructionStr(inst);
     }
  }
  else return "Field address exp entity\n";
}

GlobalVarEntity::GlobalVarEntity() {
   type = globalVarT;
}

GlobalVarEntity::GlobalVarEntity(const GlobalVarEntity &gve) {
   this->type = gve.type;
   this->node = gve.node;
   this->ltype = gve.ltype;
   this->value = gve.value;
   this->callsite = NULL;
   this->findex = -1;
   this->name = gve.name;
}
 

GlobalVarEntity::GlobalVarEntity(std::string name, SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype) {
   if (isFunctionPointer(ltype))
       this->type = funcptrGlobalVarT;
   else if (ltype->isPointerTy())
       this->type = pointerGlobalVarT;
   else if (ltype->isStructTy())
       this->type = structGlobalVarT;
   else
       this->type = primitiveGlobalVarT;
   this->node = node;
   this->ltype = ltype;
   this->value = value;
   this->callsite = NULL;
   this->findex = -1;
   this->name = name;
}

std::string GlobalVarEntity::getName() {
   return name;
}

bool GlobalVarEntity::matches(Entity *en) {
   if (en->getType() != type)
      return false;
   GlobalVarEntity *e = (GlobalVarEntity*)en;
   if (node && e->node && node->getId() != e->node->getId())
      return false;
   if (ltype && e->ltype && getTypeName(ltype) != getTypeName(e->ltype))
      return false;
   if (name == e->getName())
      return false;
   return true;
}

std::string GlobalVarEntity::getStringRep() {
   std::string res = "";
   res += "Global var name=" + name;
   return res;  
}

std::string GlobalVarEntity::toString() {
   return getStringRep();
}


UseGlobalVarEntity::UseGlobalVarEntity() {
   type = useGlobalVarT;
}

UseGlobalVarEntity::UseGlobalVarEntity(const UseGlobalVarEntity &uge) {
   this->type = uge.type;
   this->node = uge.node;
   this->ltype = uge.ltype;
   this->value = uge.value;
   this->callsite = NULL;
   this->findex = -1;
   this->name = uge.name;
}

UseGlobalVarEntity::UseGlobalVarEntity(std::string name, SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype) {
   assert(node != NULL && "Use entity needs to have an SVFG node\n");
   // to do: check node to be a MU type
   if (isFunctionPointer(ltype))
      this->type = usefuncptrGlobalVarT;
   else if (ltype->isPointerTy())
      this->type = usepointerGlobalVarT;
   else if (ltype->isStructTy())       
      this->type = usestructGlobalVarT;
   else
      this->type = useprimitiveGlobalVarT;
    
   this->name = name;
   this->node = node;
   this->value = value;
   this->ltype = ltype;
   this->callsite = NULL;
   this->findex = -1;
}

std::string UseGlobalVarEntity::getName() {
   return name;
}

bool UseGlobalVarEntity::matches(Entity *en) {
   
   if (en->getType() != type)
      return false;
   UseGlobalVarEntity *e = (UseGlobalVarEntity*)en;
   if (node && e->node && node->getId() != e->node->getId())
      return false;
   if (ltype && e->ltype && getTypeName(ltype) != getTypeName(e->ltype))
      return false;
   if (name == e->getName())
      return false;
   return true;
}

std::string UseGlobalVarEntity::getStringRep() {
  std::string res = "Use of " + name + " node=";
  if (node)
      res += std::to_string(node->getId());
  else res += "unknown";
  return res;
}

std::string UseGlobalVarEntity::toString() {
  return getStringRep();
}

DefGlobalVarEntity::DefGlobalVarEntity() {
 type = defGlobalVarT;
}

DefGlobalVarEntity::DefGlobalVarEntity(const DefGlobalVarEntity &dge) {
   this->type = dge.type;
   this->node = dge.node;
   this->ltype = dge.ltype;
   this->value = dge.value;
   this->callsite = NULL;
   this->findex = -1;
   this->name = dge.name;
}

DefGlobalVarEntity::DefGlobalVarEntity(std::string name, SVFGNode *node, 
                                               const llvm::Value *value, const llvm::Type *ltype) {
   assert(node != NULL && "Use entity needs to have an SVFG node\n");
   // to do: check node to be CHI type
   assert((type == defprimitiveGlobalVarT) || (type == defpointerGlobalVarT) ||
          (type == defstructGlobalVarT) || (type == deffuncptrGlobalVarT));
   this->type = type;
   this->name = name;
   this->node = node;
   this->value = value;
   this->ltype = ltype;
   this->callsite = NULL;
   this->findex = -1;  
}

std::string DefGlobalVarEntity::getName() {
   return name;
}

bool DefGlobalVarEntity::matches(Entity *en) {
   if (en->getType() != type)
      return false;
   DefGlobalVarEntity *e = (DefGlobalVarEntity*)en;
   if (node && e->node && node->getId() != e->node->getId())
      return false;
   if (ltype && e->ltype && getTypeName(ltype) != getTypeName(e->ltype))
      return false;
   if (name == e->getName())
      return false;
   return true;   
}

std::string DefGlobalVarEntity::getStringRep() {
  std::string res = "Def of " + name + " node=";
  if (node)
      res += std::to_string(node->getId());
  else res += "unknown";
  return res;
}

std::string DefGlobalVarEntity::toString() {
  return getStringRep();
}

UseFunctionArgEntity::UseFunctionArgEntity() {
  type = useFuncArgT;
}
 
DefFunctionArgEntity::DefFunctionArgEntity() {
  type = useFuncArgT;
}

FunctionArgEntity::FunctionArgEntity() {
   type = funcArgT;
}

FunctionArgEntity::FunctionArgEntity(const FunctionArgEntity& fae) {
  type = fae.type;
  node = fae.node;
  ltype = fae.ltype;
  value = fae.value;
  callsite = fae.callsite;
  findex = fae.findex;
}

FunctionArgEntity::FunctionArgEntity(SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype) {
   if (isFunctionPointer(ltype))
       this->type = funcptrFuncArgT;
   else if (ltype->isPointerTy())
       this->type = pointerFuncArgT;
   else if (ltype->isStructTy())
       this->type = structFuncArgT;
   else
       this->type = primitiveFuncArgT;
   this->node = node;
   this->value = value;
   this->ltype = ltype;
   this->callsite = NULL;
   this->findex = -1;  
}

llvm::Function *FunctionArgEntity::getFunction() {
  return (llvm::Function*)value;
}


bool FunctionArgEntity::matches(Entity *en) {
   if (en->getType() != type)
      return false;
   FunctionArgEntity *e = (FunctionArgEntity*)en;
   if (e->getValue() && value &&
        (((llvm::Function*)e->getValue())->getName() != ((llvm::Function*)value)->getName()))
      return false;
   if (e->getLType() && ltype && getTypeName(e->getLType()) != getTypeName(ltype))
      return false;
   if (callsite && e->getCallsite() && 
       getInstructionStr(callsite->getInstruction()) != getInstructionStr(e->getCallsite()->getInstruction()))
      return false;
   return true;
}

std::string FunctionArgEntity::getStringRep() {
   std::string res = "";
   if (value)
      res += " inside " + ((llvm::Function*)value)->getName().str();
   if (ltype)
      res += " arg type=" + getTypeName(ltype);
   if (callsite)
      res += " called within " + callsite->getCaller()->getName().str();
   if (ActualParmSVFGNode *ap = SVFUtil::dyn_cast<ActualParmSVFGNode>(node)) {
      CallSite acs = ap->getCallSite();
      std::string str = getInstructionStr(acs.getInstruction());
      res += " callsite " + str + " " + SVFUtil::getSourceLoc(acs.getInstruction());
   }
   if (SVFUtil::isa<ActualINSVFGNode>(node)) {
      ActualINSVFGNode *ainnode = SVFUtil::dyn_cast<ActualINSVFGNode>(node);
      assert(ainnode);
      CallSite acs = ainnode->getCallSite();
      std::string str1 = getInstructionStr(acs.getInstruction());
      res += " callsite " + str1 + " " + SVFUtil::getSourceLoc(acs.getInstruction());
    }
    else if (SVFUtil::isa<ActualOUTSVFGNode>(node)) {
      ActualOUTSVFGNode *ainnode = SVFUtil::dyn_cast<ActualOUTSVFGNode>(node);
      assert(ainnode);
      CallSite acs = ainnode->getCallSite();
      std::string str1 = getInstructionStr(acs.getInstruction());
      res += " callsite " + str1;
    }
   return res;
}

std::string FunctionArgEntity::toString() {
  return getStringRep();
}

 
UseFunctionArgEntity::UseFunctionArgEntity(const UseFunctionArgEntity &ufe) {
  type = ufe.type;
  node = ufe.node;
  ltype = ufe.ltype;
  value = ufe.value;
  callsite = ufe.callsite;
  findex = ufe.findex;
  argNo = ufe.argNo;
}


DefFunctionArgEntity::DefFunctionArgEntity(const DefFunctionArgEntity &ufe) {
  type = ufe.type;
  node = ufe.node;
  ltype = ufe.ltype;
  value = ufe.value;
  callsite = ufe.callsite;
  findex = ufe.findex;
  argNo = ufe.argNo;
}

UseFunctionArgEntity::UseFunctionArgEntity(int pos, SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype, 
                                           const llvm::CallSite *cs) {
   if (isFunctionPointer(ltype))
       this->type = usefuncptrFuncArgT;
   else if (ltype->isPointerTy())
       this->type = usepointerFuncArgT;
   else if (ltype->isStructTy())
       this->type = usestructFuncArgT;
   else
       this->type = useprimitiveFuncArgT;
   argNo = pos;
   this->node = node;
   this->value = value;
   this->ltype = ltype;
   this->callsite = cs;
   this->findex = -1;
}
     

DefFunctionArgEntity::DefFunctionArgEntity(int pos, SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype,
                                           const llvm::CallSite *cs) {
   if (isFunctionPointer(ltype))
       this->type = deffuncptrFuncArgT;
   else if (ltype->isPointerTy())
       this->type = defpointerFuncArgT;
   else if (ltype->isStructTy())
       this->type = defstructFuncArgT;
   else
       this->type = defprimitiveFuncArgT;
   argNo = pos;
   this->node = node;
   this->value = value;
   this->ltype = ltype;
   this->callsite = cs;
   this->findex = -1;
}

const llvm::Function *UseFunctionArgEntity::getFunction() { 
  return ((llvm::Function*)value); 
}

const llvm::Function *DefFunctionArgEntity::getFunction() {
  return ((llvm::Function*)value);
}

int UseFunctionArgEntity::getArgNo() {
  return argNo;
}

int DefFunctionArgEntity::getArgNo() {
  return argNo;
}

bool UseFunctionArgEntity::matches(Entity *en) {
   if (en->getType() != type)
      return false;
   UseFunctionArgEntity *e = (UseFunctionArgEntity*)en;
   if (e->getValue() && value && 
        (((llvm::Function*)e->getValue())->getName() != ((llvm::Function*)value)->getName()))
      return false;
   if (e->getLType() && ltype && getTypeName(e->getLType()) != getTypeName(ltype))
      return false;
   if (argNo != e->getArgNo())
      return false;
   if (callsite && e->getCallsite() && callsite->getCaller()->getName() != e->getCallsite()->getCaller()->getName())
      return false;
   return true; 
}

bool DefFunctionArgEntity::matches(Entity *en) {
   if (en->getType() != type)
      return false;
   DefFunctionArgEntity *e = (DefFunctionArgEntity*)en;
   if (e->getValue() && value &&
        (((llvm::Function*)e->getValue())->getName() != ((llvm::Function*)value)->getName()))
      return false;
   if (e->getLType() && ltype && getTypeName(e->getLType()) != getTypeName(ltype))
      return false;
   if (argNo != e->getArgNo())
      return false;
   if (callsite && e->getCallsite() && callsite->getCaller()->getName() != e->getCallsite()->getCaller()->getName())
      return false;
   return true;
}

std::string UseFunctionArgEntity::getStringRep() {
   std::string res = "";
   if (value)
      res += " inside " + ((llvm::Function*)value)->getName().str();
   res += " arg no= " + std::to_string(argNo);
   if (ltype)
      res += " arg type=" + getTypeName(ltype);
   if (callsite)
      res += " called within " + callsite->getCaller()->getName().str();
   return res; 
}

std::string DefFunctionArgEntity::getStringRep() {
   std::string res = "";
   if (value)
      res += " inside " + ((llvm::Function*)value)->getName().str();
   res += " arg no= " + std::to_string(argNo);
   if (ltype)
      res += " arg type=" + getTypeName(ltype);
   if (callsite)
      res += " called within " + callsite->getCaller()->getName().str();
   return res;
}

std::string  UseFunctionArgEntity::toString() {
  return getStringRep();
}

std::string  DefFunctionArgEntity::toString() {
  return getStringRep();
}

DataEntity::DataEntity() {
   this->type = structT;
   this->node = NULL; 
   this->ltype = NULL;
}
    
DataEntity::DataEntity(SVFGNode *node, const llvm::Type *ltype) : Entity(structT, node, ltype, -1, NULL, NULL) {
}

std::string DataEntity::getName() {
   if (ltype && ltype->isStructTy())
      return ltype->getStructName().str();
   else return "";
}

const Type *DataEntity::getLType() {
   return (Type*)ltype;
}

bool DataEntity::matches(Entity *e) {
  if (e->getType() != structT)
     return false;
  DataEntity *de = (DataEntity*)e;
  return (de->getLType() && ltype && getTypeName(de->getLType()) == getTypeName(ltype));
}

std::string DataEntity::getStringRep() {
  return getTypeName(ltype);
}

std::string DataEntity::toString() {
   return getStringRep();
}

FieldEntity::FieldEntity() {
   type = primitivedatafieldT;
}

FieldEntity::FieldEntity(SVFGNode *node, const llvm::Type *ltype, int index, const llvm::Type *ftype) : 
       Entity(primitivedatafieldT, node, ltype, index, NULL, NULL) {
   fieldType = ftype;
   if (fieldType->isPointerTy())
       type = pointerdatafieldT; 
}

std::string FieldEntity::getStructTypeName() {
     if (ltype && ltype->isStructTy())
      return ltype->getStructName();
   else return "";
}

const Type *FieldEntity::getLType() {
   return (Type*)ltype;
}

int FieldEntity::getIndexInType() {
   return findex;
}

bool FieldEntity::matches(Entity *e) {
  if (e->getType() != primitivedatafieldT && 
      e->getType() != pointerdatafieldT &&  
      e->getType() != funcptrfieldT)
     return false;
  FieldEntity *fe = (FieldEntity*)e; 
  return (fe->getLType() && fe->getFieldType() && ltype && fieldType && 
      getTypeName(fe->getLType()) == getTypeName(ltype) && 
      getTypeName(fe->getFieldType()) == getTypeName(fieldType)) && 
      fe->getIndex() == findex;
}

std::string FieldEntity::getStringRep() {
   return getTypeName(ltype);
}

std::string FieldEntity::toString() {
  return getTypeName(ltype) + " index " + std::to_string(findex) + " " + getTypeName(fieldType);
}

FuncPtrFieldEntity::FuncPtrFieldEntity() {
   type = funcptrfieldT;
}

FuncPtrFieldEntity::FuncPtrFieldEntity(SVFGNode *node, const llvm::Type *ltype, int findex, const llvm::Type *fieldType) : 
    FieldEntity(node, ltype, findex, fieldType) {
   type = funcptrfieldT;
   if (fieldType->isPointerTy()) {
      llvm::Type *et = fieldType->getPointerElementType(); 
      if (!et->isFunctionTy()) {
         assert(0 && "Field is not a function pointer!\n");
      }
      else {
         llvm::FunctionType *ft = (llvm::FunctionType*)et;
         for(int i=0; i<ft->getNumParams(); i++)
            argTypes.push_back(ft->getParamType(i));
         retType = ft->getReturnType();
      } 
   }
   //llvm::errs() << "Funcptrfieldentity cons: " << getStringRep() << "\n";
}

const llvm::Type *FuncPtrFieldEntity::getArgType(int i) {
   assert(i < argTypes.size());
   return argTypes[i];
}

int FuncPtrFieldEntity::getNumArgs() {
   return argTypes.size();
}

const llvm::Type *FuncPtrFieldEntity::getReturnType() {
   return retType;
}

std::string FuncPtrFieldEntity::toString() {
   return getStringRep();
}

std::string FuncPtrFieldEntity::getStringRep() {
  if (ltype == NULL || fieldType == NULL)
     return "Unknown type";
  std::string res = getTypeName(ltype) + " index=" + std::to_string(findex) + " " + getTypeName(fieldType);
  res += "(*)(";
  for(int i=0; i<argTypes.size(); i++) {
      res += getTypeName(argTypes[i]);
      if ( i != argTypes.size() - 1)
         res += ",";
  }
  res += ")";
  return res;
}

FunctionEntity::FunctionEntity() {
   type = functionT;
}

FunctionEntity::FunctionEntity(SVFGNode *node, const llvm::Function *value) : Entity(functionT, node, NULL, -1, value, NULL) {
}

std::string FunctionEntity::getName() {
   if (value)
      return ((Function*)value)->getName();
   return "";
}

const Function *FunctionEntity::getFunction() {
   return (Function*)value;
}

bool FunctionEntity::matches(Entity *e) {
   if (e->getType() != functionT)
      return false;
   FunctionEntity *fe = (FunctionEntity*)e;
   return (fe->getValue() && value && ((Function*)fe->getValue())->getName() == ((Function*)value)->getName());
}

std::string FunctionEntity::getStringRep() {
  return ((Function*)value)->getName();
}

std::string FunctionEntity::toString() {
  return getStringRep(); 
}
 
CallsiteEntity::CallsiteEntity() {
  type = callsiteT;
}

CallsiteEntity::CallsiteEntity(SVFGNode *node, const llvm::CallSite *cs) : Entity(callsiteT, node, NULL, -1, NULL, cs) {
   int i=0;
   for(CallSite::arg_iterator itA = cs->arg_begin(), ieA = cs->arg_end(); itA!=ieA; ++itA, i++) {
       args.push_back(*itA);
   }
   numArgs = i;
}

int CallsiteEntity::getNumArgs() { 
  return numArgs;
}

llvm::Value *CallsiteEntity::getArg(int i) {
   assert(i < numArgs && "Invalid arg value of a callsite entity!\n");
   return args[i];
}
     
std::string CallsiteEntity::getContextName() {
   if (!callsite) return "";
   llvm::Function *cf =  ((llvm::Function*)callsite->getCaller());
   return cf->getName();
}

const llvm::Function *CallsiteEntity::getFunctionContext() {
   return ((Function*)callsite->getCaller());
}

bool CallsiteEntity::matches(Entity *e) {
  if (e->getType() != callsiteT)
     return false;
  CallsiteEntity *ce = (CallsiteEntity*)e;
  return ((ce->getCallsite() != NULL) && (callsite != NULL) &&   
     ((llvm::Function*)ce->getCallsite()->getCaller())->getName() == 
     ((llvm::Function*)callsite->getCaller())->getName());
}

std::string CallsiteEntity::getStringRep() {
  if  (callsite) {
      std::string str;
      llvm::raw_string_ostream rso(str);
      rso << (*callsite->getInstruction()) << "\n";
      std::string fname = getFunctionName(rso.str());
      if (fname != "") {
         return fname + "()";
      }
      Function *f = NULL;
      if (!callsite->getCalledValue() && (f = (Function*)callsite->getCaller())) {
         std::string res = "function=" + f->getName().str() + " ";          
         return res + rso.str();
      }
      else return rso.str();
  }  
  else return "";
}
     
std::string CallsiteEntity::toString() {
   return getStringRep();
}

Solution::Solution() {
   type = nonestype;
}


void Solution::print() {}

Label *Solution::getLabel() {
  return label;
}

void Solution::setLabel(Label *label) {
  this->label = label;
}
     
UnarySolution::UnarySolution() {
   type = unarySol;
}

UnarySolution::UnarySolution(Label *label, std::set<Entity*> sol) {
   this->label = label;
   type = unarySol;
   results = sol;
   for(auto e : sol) {
      if (e->getLType()) {
         ltypes.insert(e->getLType());
         ltypeEntityMap[e->getLType()] = e;
         if (e->getIndex()) {
            fields.insert(std::make_pair(e->getLType(), e->getIndex()));
            fieldEntityMap[std::make_pair(e->getLType(), e->getIndex())] = e;
         }
      }
      if (e->getValue()) {
         values.insert(e->getValue());
         valueEntityMap[e->getValue()] = e; 
      }
      if (e->getCallsite()) {
         callsites.insert(e->getCallsite());
         callsiteEntityMap[e->getCallsite()] = e;
      }
   }
}

int UnarySolution::getSize() {
  return results.size();
}

void UnarySolution::print() {
   llvm::errs() << "Solution size=" << results.size() << "\n";
   for(auto r : results) 
       llvm::errs() << r->toString() << "\n"; 
}

bool UnarySolution::isAMember(Entity *entity) {
   for(auto r : results)
      if (r->matches(entity))
          return true;
   return false;
}

BinarySolution *UnarySolution::filterIndirectCallOf(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::indirectCallOf(r1, r2)) {
                //llvm::errs() << "adding indcall pair to results\n";
               res.insert(std::make_pair(r1, r2));
            }
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::indirectCallOf(r1, r2.first)) {
               //llvm::errs() << "adding indcall pair to results\n";
               res.insert(std::make_pair(r1, r2.first));
            }
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::indirectCallOf(r1, std::get<0>(r2))) {
               //llvm::errs() << "adding indcall pair to results\n";
               res.insert(std::make_pair(r1, std::get<0>(r2)));
            }
         }
      }
   }
   return new BinarySolution(l, res);
}

BinarySolution *UnarySolution::filterDefinesCallback(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::definesCallback(r1, r2)) 
               res.insert(std::make_pair(r1, r2)); 
         }
      }
   } 
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::definesCallback(r1, r2.first))
               res.insert(std::make_pair(r1, r2.first));
         }
      }      
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::definesCallback(r1, std::get<0>(r2)))
               res.insert(std::make_pair(r1, std::get<0>(r2)));
         }
      }
   }
   return new BinarySolution(l, res);
}

int BinarySolution::getSize() {
  return results.size();
}

BinarySolution *UnarySolution::filterActualparameterpassedby(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::actualparameterpassedby(r1, r2))
               res.insert(std::make_pair(r1, r2));
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::actualparameterpassedby(r1, r2.first))
               res.insert(std::make_pair(r1, r2.first));
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::actualparameterpassedby(r1, std::get<0>(r2)))
               res.insert(std::make_pair(r1, std::get<0>(r2)));
         }
      }
   }
   return new BinarySolution(l, res);
}

BinarySolution *UnarySolution::filterTaints(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::taints(r1, r2))
               res.insert(std::make_pair(r1, r2));
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::taints(r1, r2.first))
               res.insert(std::make_pair(r1, r2.first));
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::taints(r1, std::get<0>(r2)))
               res.insert(std::make_pair(r1, std::get<0>(r2)));
         }
      }
   }
   return new BinarySolution(l, res);
}


std::map<SVFGNode*, Entity*> UnarySolution::getNodeMap() {
   std::map<SVFGNode*, Entity*> m;
   for(auto n : results)
       if (n->getNode())
          m[n->getNode()] = n;
   return m;
}

std::set<SVFGNode*> UnarySolution::getNodes() {
   std::set<SVFGNode*> res;
   for(auto n : results)
       if (n->getNode())
          res.insert(n->getNode());
   return res;
}


std::set<NodeID> UnarySolution::getNodeIds() {
   std::set<NodeID> res;
   for(auto n : results)
       if (n->getNode())
          res.insert(n->getNode()->getId());
   return res;
}

BinarySolution::BinarySolution() {
   type = binarySol;
}

BinarySolution::BinarySolution(Label *label, std::set<std::pair<Entity*,Entity*> > sol) {
   assert(label->isRelational() && "Cannot create a binary solution if the label is not binary\n");
   type = binarySol;
   this->label = label;
   results = sol;
   for(auto e : sol) {
      if (e.first->getLType()) {
         ltypes.insert(e.first->getLType());
         ltypeEntityMap[e.first->getLType()] = e.first;
         if (e.first->getIndex() != -1) {
            fields.insert(std::make_pair(e.first->getLType(), e.first->getIndex()));
            fieldEntityMap[std::make_pair(e.first->getLType(), e.first->getIndex())] = e.first;
         }
      }
      if (e.first->getValue()) {
         values.insert(e.first->getValue());
         valueEntityMap[e.first->getValue()] = e.first; 
      }
      if (e.first->getCallsite()) {
         callsites.insert(e.first->getCallsite());
         callsiteEntityMap[e.first->getCallsite()] = e.first;
      }
   }    
}

void BinarySolution::print() {
   llvm::errs() << "Solution size=" << results.size() << "\n";
   for(auto r : results) 
       llvm::errs() << "(" << r.first->toString() 
                    << "," << r.second->toString() 
                    << ")\n";     
}

bool BinarySolution::isAMember(Entity *entity) {
   for(auto rm : results) {
      if (rm.first->matches(entity))
         return true;
   }
   return false;
}

BinarySolution *BinarySolution::reverse() {
    BinarySolution *rev = new BinarySolution();
    for(auto r : results) {
        rev->results.insert(std::make_pair(r.second,r.first));
    }
    for(auto r : rev->results) {
       if (r.second->getLType()) {
          rev->ltypes.insert(r.second->getLType());
          rev->ltypeEntityMap[r.second->getLType()] = r.second;
          if (r.second->getIndex() != -1) {
             rev->fields.insert(std::make_pair(r.second->getLType(), r.second->getIndex()));
             rev->fieldEntityMap[std::make_pair(r.second->getLType(), r.second->getIndex())] = r.second;
          } 
       }
       if (r.second->getValue()) {
          rev->values.insert(r.second->getValue());
          rev->valueEntityMap[r.second->getValue()] = r.second;
       }
       if (r.second->getCallsite()) {
          rev->callsites.insert(r.second->getCallsite());
          rev->callsiteEntityMap[r.second->getCallsite()] =  r.second;
       }
    }
    return rev;
}


BinarySolution *BinarySolution::filterIndirectCallOf(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::indirectCallOf(r1.first, r2)) {
               //llvm::errs() << "adding indcall pair to results\n";
               res.insert(std::make_pair(r1.first, r2));
            }
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::indirectCallOf(r1.first, r2.first)) {
               //llvm::errs() << "adding indcall pair to results\n";
               res.insert(std::make_pair(r1.first, r2.first));
            }
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::indirectCallOf(r1.first, std::get<0>(r2))) {
               //llvm::errs() << "adding indcall pair to results\n";
               res.insert(std::make_pair(r1.first, std::get<0>(r2)));
            }
         }
      }
   }
   return new BinarySolution(l, res);
}

BinarySolution *BinarySolution::filterDefinesCallback(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::definesCallback(r1.first, r2))
               res.insert(std::make_pair(r1.first, r2));
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::definesCallback(r1.first, r2.first))
               res.insert(std::make_pair(r1.first, r2.first));
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::definesCallback(r1.first, std::get<0>(r2)))
               res.insert(std::make_pair(r1.first, std::get<0>(r2)));
         }
      }
   }
   return new BinarySolution(l, res);
}

BinarySolution *BinarySolution::filterActualparameterpassedby(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::actualparameterpassedby(r1.first, r2))
               res.insert(std::make_pair(r1.first, r2));
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::actualparameterpassedby(r1.first, r2.first))
               res.insert(std::make_pair(r1.first, r2.first));
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::actualparameterpassedby(r1.first, std::get<0>(r2)))
               res.insert(std::make_pair(r1.first, std::get<0>(r2)));
         }
      }
   }
   return new BinarySolution(l, res);
}


BinarySolution *BinarySolution::filterTaints(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::taints(r1.first, r2))
               res.insert(std::make_pair(r1.first, r2));
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::taints(r1.first, r2.first))
               res.insert(std::make_pair(r1.first, r2.first));
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::taints(r1.first, std::get<0>(r2)))
               res.insert(std::make_pair(r1.first, std::get<0>(r2)));
         }
      }
   }
   return new BinarySolution(l, res);
}

std::map<SVFGNode*, Entity*> BinarySolution::getNodeMap() {
   std::map<SVFGNode*, Entity*> m;
   for(auto n : results)
       if (n.first->getNode())
          m[n.first->getNode()] = n.first;
   return m;
}

std::set<SVFGNode*> BinarySolution::getNodes() {
   std::set<SVFGNode*> res;
   for(auto n : results)
       if (n.first->getNode())
          res.insert(n.first->getNode());
   return res;
}


std::set<NodeID> BinarySolution::getNodeIds() {
   std::set<NodeID> res;
   for(auto n : results)
       if (n.first->getNode())
          res.insert(n.first->getNode()->getId());
   return res;
}

std::set<Entity*> BinarySolution::getResults() {
   std::set<Entity*> res;
   for(auto rm : results) 
      res.insert(rm.first);
   return res;
}


TernarySolution::TernarySolution() { type = ternarySol; }

TernarySolution::TernarySolution(Label *label, std::set<std::tuple<Entity*,Entity*,Entity*> > sol) {
   assert(label->isRelational() && "Cannot create a binary solution if the label is not binary\n");
   type = ternarySol;
   this->label = label;
   results = sol;
   for(auto e : sol) {
      if (std::get<0>(e)->getLType()) {
         ltypes.insert(std::get<0>(e)->getLType());
         ltypeEntityMap[std::get<0>(e)->getLType()] = std::get<0>(e);
         if (std::get<0>(e)->getIndex() != -1) {
            fields.insert(std::make_pair(std::get<0>(e)->getLType(), std::get<0>(e)->getIndex()));
            fieldEntityMap[std::make_pair(std::get<0>(e)->getLType(), std::get<0>(e)->getIndex())] = std::get<0>(e);
         }
      }
      if (std::get<0>(e)->getValue()) {
         values.insert(std::get<0>(e)->getValue());
         valueEntityMap[std::get<0>(e)->getValue()] = std::get<0>(e);
      }
      if (std::get<0>(e)->getCallsite()) {
         callsites.insert(std::get<0>(e)->getCallsite());
         callsiteEntityMap[std::get<0>(e)->getCallsite()] = std::get<0>(e);
      }
   }
}

void TernarySolution::print() {
   llvm::errs() << "Solution size=" << results.size() << "\n";
   for(auto r : results)
       llvm::errs() << "(" << std::get<0>(r)->toString()
                    << "," << std::get<1>(r)->toString()
                    << "," << std::get<2>(r)->toString()
                    << ")\n";
}

bool TernarySolution::isAMember(Entity *entity) {
   for(auto rm : results) {
      if (std::get<0>(rm)->matches(entity))
         return true;
   }
   return false;
}

int TernarySolution::getSize() {
  return results.size();
}

std::map<SVFGNode*, Entity*> TernarySolution::getNodeMap() {
   std::map<SVFGNode*, Entity*> m;
   for(auto n : results)
       if (std::get<0>(n)->getNode())
          m[std::get<0>(n)->getNode()] = std::get<0>(n);
   return m;
}

std::set<SVFGNode*> TernarySolution::getNodes() {
   std::set<SVFGNode*> res;
   for(auto n : results)
       if (std::get<0>(n)->getNode())
          res.insert(std::get<0>(n)->getNode());
   return res;
}

std::set<NodeID> TernarySolution::getNodeIds() {
   std::set<NodeID> res;
   for(auto n : results)
       if (std::get<0>(n)->getNode())
          res.insert(std::get<0>(n)->getNode()->getId());
   return res;
}

BinarySolution *TernarySolution::filterDefinesCallback(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::definesCallback(std::get<0>(r1), r2))
               res.insert(std::make_pair(std::get<0>(r1), r2));
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::definesCallback(std::get<0>(r1), r2.first))
               res.insert(std::make_pair(std::get<0>(r1), r2.first));
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::definesCallback(std::get<0>(r1), std::get<0>(r2)))
               res.insert(std::make_pair(std::get<0>(r1), std::get<0>(r2)));
         }
      }
   }
   return new BinarySolution(l, res);
}

BinarySolution *TernarySolution::filterIndirectCallOf(Label *l, Solution *sol) {
  std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::indirectCallOf(std::get<0>(r1), r2)) {
               //llvm::errs() << "adding indcall pair to results\n";
               res.insert(std::make_pair(std::get<0>(r1), r2));
            }
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::indirectCallOf(std::get<0>(r1), r2.first)) {
               //llvm::errs() << "adding indcall pair to results\n";
               res.insert(std::make_pair(std::get<0>(r1), r2.first));
            }
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::indirectCallOf(std::get<0>(r1), std::get<0>(r2))) {
               //llvm::errs() << "adding indcall pair to results\n";
               res.insert(std::make_pair(std::get<0>(r1), std::get<0>(r2)));
            }
         }
      }
   }
   return new BinarySolution(l, res);
}

BinarySolution *TernarySolution::filterActualparameterpassedby(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::actualparameterpassedby(std::get<0>(r1), r2))
               res.insert(std::make_pair(std::get<0>(r1), r2));
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::actualparameterpassedby(std::get<0>(r1), r2.first))
               res.insert(std::make_pair(std::get<0>(r1), r2.first));
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::actualparameterpassedby(std::get<0>(r1), std::get<0>(r2)))
               res.insert(std::make_pair(std::get<0>(r1), std::get<0>(r2)));
         }
      }
   }
   return new BinarySolution(l, res);
}

BinarySolution *TernarySolution::filterTaints(Label *l, Solution *sol) {
   std::set<std::pair<Entity*, Entity*> > res;
   if (sol->getType() == unarySol) {
      for(auto r1: results) {
         for(auto r2: ((UnarySolution*)sol)->results) {
            if (Entity::taints(std::get<0>(r1), r2))
               res.insert(std::make_pair(std::get<0>(r1), r2));
         }
      }
   }
   else if (sol->getType() == binarySol) {
      for(auto r1: results) {
         for(auto r2: ((BinarySolution*)sol)->results) {
            if (Entity::taints(std::get<0>(r1), r2.first))
               res.insert(std::make_pair(std::get<0>(r1), r2.first));
         }
      }
   }
   else if (sol->getType() == ternarySol) {
      for(auto r1: results) {
         for(auto r2: ((TernarySolution*)sol)->results) {
            if (Entity::taints(std::get<0>(r1), std::get<0>(r2)))
               res.insert(std::make_pair(std::get<0>(r1), std::get<0>(r2)));
         }
      }
   }
   return new BinarySolution(l, res);
}

std::set<Entity*> TernarySolution::getResults() {
   std::set<Entity*> res;
   for(auto rm : results)
      res.insert(std::get<0>(rm));
   return res;
}
