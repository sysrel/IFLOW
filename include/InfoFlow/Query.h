#ifndef _QUERY_H
#define _QUERY_H

#include "DDA/DDAStat.h"
#include "DDA/FlowDDA.h"
#include "DDA/ContextDDA.h"
#include "MemoryModel/PointerAnalysis.h"
#include "Util/SVFUtil.h"
#include "Util/ICFGNode.h"
#include "MSSA/SVFGStat.h"
#include "MSSA/MemRegion.h"
#include "MSSA/MemSSA.h"
#include <llvm/Support/CommandLine.h> 
#include "MemoryModel/PAG.h"


class Entity;
class Label;
class Constraint;
class Query;
class Solution;
class UnarySolution;
class BinarySolution;
class TernarySolution;

std::string getTypeName(const llvm::Type *);

enum QTYPE {regexp, relationalexp, noneqtype};

enum UTYPE {calls, calledby, /* (functionT, functionT) ()*/
            taints, /* (source, sink), source: defs of global variables and field types, sink: use return values, use func args*/
            taintedby,  /* (sink, source) */
            sanitizes, /* (source, sink) sink: use function args, operands */ 
            pointedby,
            defines, /* (def, globalVarT) (def, fieldT) (def, funcArgT) */
            uses, /* (use, def) */
            indirectcallof, /* callsite,  funcptrfieldT*/
            formalparameterpasses, formalparameterpassedby, /*(functionT, funcArgT) */
            actualparameterpasses, actualparameterpassedby, /*(callsite, globalVarT) *(callsite, fieldT) */
            definescallback, callbackdefinedin, /* (structT, funcptrfieldT)*/  
            hasfield, fieldof,  /*(structT, fieldT)*/
            oftype, istypeof, /*(funcArgT, strucT) ((funcArgT,fieldT) */
            reachesRel, 
            taintsbothinseq,
            noneutype};

enum ETYPE {structT, 
            fieldT, // generic
            primitivedatafieldT, 
            pointerdatafieldT, 
            funcptrfieldT, 
            globalVarT,
            primitiveGlobalVarT,
            pointerGlobalVarT,
            structGlobalVarT,
            funcptrGlobalVarT,
            useGlobalVarT, // generic 
            useprimitiveGlobalVarT,   
            usestructGlobalVarT, 
            usepointerGlobalVarT, 
            usefuncptrGlobalVarT, 
            defGlobalVarT, // generic
            defprimitiveGlobalVarT,   
            defstructGlobalVarT, 
            defpointerGlobalVarT, 
            deffuncptrGlobalVarT,           
            funcArgT,  // generic
            primitiveFuncArgT,
            pointerFuncArgT,
            funcptrFuncArgT,
            structFuncArgT, 
            useFuncArgT, // generic
            useprimitiveFuncArgT,
            usepointerFuncArgT,
            usefuncptrFuncArgT,
            usestructFuncArgT,
            deffuncArgT,  // generic
            defprimitiveFuncArgT,
            defpointerFuncArgT,
            deffuncptrFuncArgT,
            defstructFuncArgT,
            functionT, 
            callsiteT, 
            genericValueT,
            addrExprT,
            fieldAddrExprT,
            noneetype};

enum CTYPE {andComp, orComp, nonectype};
enum LTYPE {unaryT, binaryT, noneltype};
enum STYPE {unarySol, binarySol, ternarySol, nonestype};

class Identifier {
   protected: 
   std::string name;
   ETYPE entitytype; 
   std::string tname;
   int field;
   int argNo;
  public:
    Identifier();
    Identifier(const Identifier &i);
    Identifier(ETYPE entitytype, std::string name);
    Identifier(ETYPE entitytype, std::string name, int argno);
    Identifier(ETYPE entitytype, std::string name, std::string tname, int field);
    ETYPE getType() { return entitytype; }
    std::string getName() { return name; }
    std::string getTypeName() { return tname; }
    int getField() { return field; }  
    int getArgNo() { return argNo; }
};

class QueryExp {
   protected:
      QTYPE type; 
   public:
      QueryExp();
      QueryExp(QTYPE type); 
      QTYPE getType() { return type; }
      virtual QueryExp *reverse() = 0;
};

class RegExpQueryExp : public QueryExp {
  protected:
    Identifier *id;
    std::string endsWith;
    std::set<std::string> doesnotendWith;
    std::string beginsWith;
    std::set<std::string> doesnotbeginWith;
    std::set<std::string> includes;
    std::set<std::string> orIncludes;
    std::set<std::string> excludes;
    std::string equals;
  public:
    RegExpQueryExp();
    RegExpQueryExp(const RegExpQueryExp&);
    RegExpQueryExp(std::string idname, ETYPE entitytype);
    RegExpQueryExp(std::string idname, ETYPE entitytype, int arg);
    RegExpQueryExp(std::string idname, ETYPE entitytype,
                   std::string tname, int field);
    Identifier *getOperand() { return id; }
    void setEndsWith(std::string);
    void addDoesNotEndWith(std::string);
    void setBeginsWith(std::string);
    void addDoesNotBeginWith(std::string); 
    void addIncludes(std::string);
    void addOrIncludes(std::string);
    void addExcludes(std::string);
    void setEquals(std::string);
    bool matches(std::string);
    virtual QueryExp *reverse();
};


class RelationalQueryExp : public QueryExp {
   protected:
      Identifier *id1;
      Identifier *id2;       
      Identifier *id3;       
      UTYPE utype;
   public: 
      RelationalQueryExp();
      RelationalQueryExp(const RelationalQueryExp&);
      RelationalQueryExp(Identifier *id1, Identifier *id2, UTYPE utype);
      RelationalQueryExp(Identifier *id1, Identifier *id2, Identifier *id3, UTYPE utype);
      UTYPE getRelationType() { return utype; }
      Identifier *getOperand1() { return id1; }
      Identifier *getOperand2() { return id2; }
      Identifier *getOperand3() { return id3; }
      virtual QueryExp *reverse();
};




class Query {
   protected:
     QueryExp *exp;
     std::map<std::string, Constraint*> constraints;     
     Constraint *sanitizationConstraint;
     std::vector<int> sanitizationReturnValue;
   public:
     Query();
     Query(QueryExp *qexp);
     void addConstraint(std::string id, Constraint *cons);
     void addConstraint(Constraint *cons);
     void setSanitizationConstraint(Constraint *cons, int returnvalue); 
     void setSanitizationConstraint(Constraint *cons, int returnvalue, int retvalue2, int retvalue3); 
     Constraint *getSanitizationConstraint();
     std::vector<int> getSanitizationReturnValue() {return sanitizationReturnValue;}
     Query *reverse();
     QueryExp *getQueryExp() { return exp; }
     std::map<std::string, Constraint*> getConstraints() { return constraints; }     
};


class Constraint {
   protected:
      Identifier *id;
      Label *label;
   public: 
      Constraint();
      Constraint(Identifier *, Label *label);       
      Label *getLabel() { return label; }
      Identifier  *getIdentifier() { return id; } 
};



class Label {
   protected:
     std::string desc;
     Query *query;
     Solution *solution;
     bool isnegated;
     bool isatomic;
   public: 
     Label();
     Label(std::string desc, Query *query);
     Label(const Label&);
     std::string getDescription() { return desc; }
     void setSolution(Solution *);
     Solution *getSolution(); 
     bool isAMember(Entity *);  
     // change the direction of the use relationship like transpose
     Label *reverse(std::string newdesc);
     // negate the relationship
     void setNegated(bool neg);
     bool isNegated();
     bool isAtomic();
     bool hasSolution(); 
     bool isRelational();
     QueryExp *getQueryExp();
     Query *getQuery() { return query; }
};



class CompositeLabel : public Label {
   protected:
      std::vector<Label*> labels;
      CTYPE composetype;
   public:
     CompositeLabel();
     CompositeLabel(std::string desc, CTYPE composetype); 
     void addLabel(Label *);    
};


class Entity {
 protected:
    ETYPE type;
    const llvm::Type *ltype;
    const llvm::Value *value;
    const llvm::CallSite *callsite;
    SVFGNode *node;
    int findex;
 public: 
    Entity();
    Entity(ETYPE type, SVFGNode *node, const llvm::Type *ltype, int index, const llvm::Value *value, const llvm::CallSite *cs); 
    ETYPE getType() { return type;  }
    const llvm::Type *getLType() { return ltype; }
    const llvm::Value *getValue() { return value; }
    const llvm::CallSite *getCallsite() { return callsite; }
    SVFGNode *getNode() { return node; } 
    int getIndex() { return findex; }
    virtual bool matches(Entity *) = 0;
    // for regular expression matching
    virtual std::string getStringRep() = 0;
    // for pretty printing
    virtual std::string toString() = 0;
    static bool definesCallback(Entity *, Entity*);
    static bool indirectCallOf(Entity *, Entity *);
    static bool actualparameterpassedby(Entity *, Entity *);
    static bool taints(Entity *, Entity *);
    static std::map<std::string, std::set<Entity*> *> generateClusters(std::set<Entity*>);
    static Entity *create(ETYPE, SVFGNode *);
};

class GenericValueEntity : public Entity {
   public:
     GenericValueEntity();
     GenericValueEntity(const GenericValueEntity&);
     GenericValueEntity(SVFGNode *node);
     virtual bool matches(Entity *);
     virtual std::string getStringRep();
     virtual std::string toString();
};

class AddrExpressionEntity : public Entity {
   public:
      AddrExpressionEntity();
      AddrExpressionEntity(const AddrExpressionEntity&);
      AddrExpressionEntity(SVFGNode *);
      virtual bool matches(Entity *);
      virtual std::string getStringRep();
      virtual std::string toString(); 
};

class FieldAddrExpressionEntity : public Entity {
   public:
      FieldAddrExpressionEntity();
      FieldAddrExpressionEntity(const FieldAddrExpressionEntity&);
      FieldAddrExpressionEntity(SVFGNode *);
      virtual bool matches(Entity *);
      virtual std::string getStringRep();
      virtual std::string toString();
};

class GlobalVarEntity : public Entity {
   protected:
     std::string name;
   public:
     GlobalVarEntity();
     GlobalVarEntity(const GlobalVarEntity &);
     GlobalVarEntity(std::string name, SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype);
     std::string getName();
     virtual bool matches(Entity *);
     virtual std::string getStringRep();
     virtual std::string toString();
};


class UseGlobalVarEntity : public Entity {
   protected:
     std::string name;
   public:
     UseGlobalVarEntity();
     UseGlobalVarEntity(const UseGlobalVarEntity &);
     UseGlobalVarEntity(std::string name, SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype);
     std::string getName();
     virtual bool matches(Entity *);
     virtual std::string getStringRep();
     virtual std::string toString();
};

class DefGlobalVarEntity : public Entity {
   protected:
     std::string name;
   public:
     DefGlobalVarEntity();
     DefGlobalVarEntity(const DefGlobalVarEntity &);
     DefGlobalVarEntity(std::string name, SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype);
     std::string getName();
     virtual bool matches(Entity *);
     virtual std::string getStringRep();
     virtual std::string toString();
};

class FunctionArgEntity: public Entity {
     protected:
     public:
        FunctionArgEntity();
        FunctionArgEntity(const FunctionArgEntity&);
        FunctionArgEntity(SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype);
        llvm::Function *getFunction();
        virtual bool matches(Entity *);
        virtual std::string getStringRep();
        virtual std::string toString();
};


class UseFunctionArgEntity: public Entity {
     protected:
       int argNo; 
     public:
        UseFunctionArgEntity();
        UseFunctionArgEntity(const UseFunctionArgEntity&);
        UseFunctionArgEntity(int pos, SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype, const llvm::CallSite *cs); 
        const llvm::Function *getFunction();
        int getArgNo();
        virtual bool matches(Entity *);
        virtual std::string getStringRep();
        virtual std::string toString();         
};


class DefFunctionArgEntity: public Entity {
     protected:
       int argNo; 
     public:
        DefFunctionArgEntity();
        DefFunctionArgEntity(const DefFunctionArgEntity&);
        DefFunctionArgEntity(int pos, SVFGNode *node, const llvm::Value *value, const llvm::Type *ltype, const llvm::CallSite *cs); 
        const llvm::Function *getFunction();
        const llvm::CallSite *getCallsite() { return callsite; }
        int getArgNo();
        virtual bool matches(Entity *);
        virtual std::string getStringRep();
        virtual std::string toString();         
};

class DataEntity : public Entity {
   protected:
   public:
   DataEntity();
   DataEntity(SVFGNode *node, const llvm::Type *value);   
   std::string getName();
   const llvm::Type *getLType();
   virtual bool matches(Entity *);
   virtual std::string getStringRep();
   virtual std::string toString();
};

class FieldEntity : public Entity {
   protected:
     const llvm::Type *fieldType;
   public:
     FieldEntity();
     FieldEntity(SVFGNode *node, const llvm::Type *value, int findex, const llvm::Type *fieldType);
     std::string getName();
     std::string getStructTypeName();
     int getIndexInType();
     const Type *getLType();
     const Type *getFieldType() { return fieldType; }
     virtual bool matches(Entity *);
     virtual std::string getStringRep();
     virtual std::string toString();
};

class FuncPtrFieldEntity : public FieldEntity {
    protected:
      std::vector<llvm::Type*> argTypes;
      llvm::Type *retType;
    public:
       FuncPtrFieldEntity();
       FuncPtrFieldEntity(SVFGNode *node, const llvm::Type *value, int findex, const llvm::Type *fieldType);
       const llvm::Type *getArgType(int i);	
       int getNumArgs();
       const llvm::Type *getReturnType();
       virtual std::string getStringRep();
       virtual std::string toString();
}; 


class FunctionEntity : public Entity {
   protected:
   public:
     FunctionEntity();
     FunctionEntity(SVFGNode *node, const llvm::Function *value);
     std::string getName();
     const llvm::Function *getFunction(); 
     virtual bool matches(Entity *);
     virtual std::string getStringRep();
     virtual std::string toString();
};

class CallsiteEntity : public Entity {
  protected:
     bool isdirect;
     int numArgs;
     std::vector<llvm::Value*> args;
  public:
     CallsiteEntity();
     CallsiteEntity(SVFGNode *node, const llvm::CallSite *value);
     std::string getContextName();
     const llvm::Function *getFunctionContext();
     int getNumArgs();
     llvm::Value *getArg(int i);
     virtual bool matches(Entity *);
     virtual std::string getStringRep();
     virtual std::string toString();
     //std::string getTargetName();
     //std::set<Function*> getTargets();  
};

class Solution {
  protected:
     Label *label;
     STYPE type;
    std::map<const llvm::Value*, Entity*> valueEntityMap;
    std::set<const llvm::Value*> values; 
    std::set<const llvm::Type*> ltypes;
    std::map<const llvm::Type*, Entity*> ltypeEntityMap;
    std::set<std::pair<const llvm::Type*, int> > fields;
    std::map<std::pair<const llvm::Type*, int>, Entity*> fieldEntityMap;
    std::set<const llvm::CallSite*> callsites;
    std::map<const llvm::CallSite*, Entity*> callsiteEntityMap;
  public:
     Solution();
     virtual void print() = 0;
     Label *getLabel();
     STYPE getType() { return type; }
     void setLabel(Label *label);
     bool isUnary() { return (type == unarySol); }
     bool isBinary() { return (type == binarySol); }
     virtual std::set<Entity*> getResults() = 0;
     virtual int getSize() = 0;
     virtual bool isAMember(Entity *entity) = 0; 
     virtual std::map<SVFGNode*, Entity*> getNodeMap() = 0;
     virtual std::set<SVFGNode*> getNodes() = 0;
     virtual std::set<NodeID> getNodeIds() = 0;
     virtual BinarySolution *filterDefinesCallback(Label *l, Solution *sol) = 0;
     virtual BinarySolution *filterIndirectCallOf(Label *l, Solution *sol) = 0;
     virtual BinarySolution *filterActualparameterpassedby(Label *l, Solution *sol) = 0;
     virtual BinarySolution *filterTaints(Label *l, Solution *sol) = 0;
};

class UnarySolution : public Solution {
  friend class BinarySolution;
  friend class TernarySolution;
  protected:
    std::set<Entity*> results;
  public:
     UnarySolution();
     UnarySolution(Label *label, std::set<Entity*> sol);
     void print();
     bool isAMember(Entity *entity);
     virtual int getSize();
     virtual std::map<SVFGNode*, Entity*> getNodeMap();
     virtual std::set<SVFGNode*> getNodes();
     virtual std::set<NodeID> getNodeIds();
     virtual BinarySolution *filterDefinesCallback(Label *l, Solution *sol);
     virtual BinarySolution *filterIndirectCallOf(Label *l, Solution *sol);
     virtual BinarySolution *filterActualparameterpassedby(Label *l, Solution *sol);
     virtual BinarySolution *filterTaints(Label *l, Solution *sol);
     virtual std::set<Entity*> getResults() { return results; }
};

class BinarySolution : public Solution {
  friend class UnarySolution;
  friend class TernarySolution;
  protected:
    std::set<std::pair<Entity*, Entity*> > results;
  public:
    BinarySolution();
    BinarySolution(Label *label, std::set<std::pair<Entity*,Entity*> > results); 
    void print();
    bool isAMember(Entity *entity);
    virtual int getSize();
    virtual std::map<SVFGNode*, Entity*> getNodeMap();
    virtual std::set<SVFGNode*> getNodes();
    virtual std::set<NodeID> getNodeIds(); 
    BinarySolution *reverse();
    virtual BinarySolution *filterDefinesCallback(Label *l, Solution *sol);
    virtual BinarySolution *filterIndirectCallOf(Label *l, Solution *sol);
    virtual BinarySolution *filterActualparameterpassedby(Label *l, Solution *sol);    
    virtual BinarySolution *filterTaints(Label *l, Solution *sol);
    virtual std::set<Entity*> getResults();
};


class TernarySolution : public Solution {
  friend class UnarySolution;
  friend class BinarySolution;
  protected:
    std::set<std::tuple<Entity*, Entity*, Entity*> > results;
  public:
    TernarySolution();
    TernarySolution(Label *label, std::set<std::tuple<Entity*,Entity*,Entity*> > results);
    void print();
    bool isAMember(Entity *entity);
    virtual int getSize();
    virtual std::map<SVFGNode*, Entity*> getNodeMap();
    virtual std::set<SVFGNode*> getNodes();
    virtual std::set<NodeID> getNodeIds();
    virtual BinarySolution *filterDefinesCallback(Label *l, Solution *sol);
    virtual BinarySolution *filterIndirectCallOf(Label *l, Solution *sol);
    virtual BinarySolution *filterActualparameterpassedby(Label *l, Solution *sol);
    virtual BinarySolution *filterTaints(Label *l, Solution *sol);
    virtual std::set<Entity*> getResults();
};

#endif
