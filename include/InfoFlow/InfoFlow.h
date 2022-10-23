#ifndef _INFOFLOW_H
#define _INFOFLOW_H

#include "SABER/SrcSnkDDA.h"
#include "InfoFlow/Query.h"
#include "Util/ICFG.h"
#include <fstream>
#include <map>

/*
class FlowAnalyzer : public CFLSrcSnkSolver {
  protected: 
    PathCondAllocator* pathCondAllocator;
    SVFGNodeToDPItemsMap nodeToDPItemsMap;      ///<  record forward visited dpitems
    SVFGNodeSet visitedSet;
    SVFG *svfg;
    SVFGBuilder memSSA;
  public:
    FlowAnalyzer();
    FlowAnalyzer(std::set<SVFGNode*> sources, std::set<SVFGNode*> dests, SVFG *svfg);
    virtual bool isSource(const SVFGNode* node);
    virtual bool isSink(const SVFGNode* node);
    virtual void reportBug(ProgSlice* slice);
    bool pathExists() { return isSomePathReachable(); }
};
*/

class InfoFlowEngine {
  protected:
   // label name to Label map
   std::map<std::string, Label*> labelmap;
   // label name to solution map
   std::map<std::string, Solution*> solutionCache;
   SVFG *svfg;
   ICFG *icfg;
   std::set<Entity*> rawdataentities;
   std::set<Entity*> rawpointerfieldentities;
   std::set<Entity*> rawprimitivefieldentities;
   std::set<Entity*> rawfuncptrfieldentities;
   std::set<Entity*> rawfunctionentities;
   std::set<Entity*> rawcallsiteentities;
   std::set<Entity*> rawglobalvariables;
   std::set<Entity*> rawpointerglobalvariables;
   std::set<Entity*> rawprimitiveglobalvariables;
   std::set<Entity*> rawfuncptrglobalvariables;
   std::set<Entity*> rawstructglobalvariables;
   std::set<Entity*> rawuseglobalvariables;
   std::set<Entity*> rawusepointerglobalvariables;
   std::set<Entity*> rawuseprimitiveglobalvariables;
   std::set<Entity*> rawusefuncptrglobalvariables;
   std::set<Entity*> rawusestructglobalvariables;
   std::set<Entity*> rawdefglobalvariables;
   std::set<Entity*> rawdefpointerglobalvariables;
   std::set<Entity*> rawdefprimitiveglobalvariables;
   std::set<Entity*> rawdeffuncptrglobalvariables;
   std::set<Entity*> rawdefstructglobalvariables;
   std::set<Entity*> rawfunctionargentities;
   void findGlobalVarEntities(std::set<Entity*> *&eset);
   void findUseGlobalVarEntities(std::set<Entity*> *&eset);
   void findUseLoadGlobalVarEntities();
   void findUseCallsiteGlobalVarEntities();
   void findUseRetGlobalVarEntities();
   void findDefGlobalVarEntities(std::set<Entity*> *&eset);
   void findDefRetGlobalVarEntities();
   void findDefStoreGlobalVarEntities();
   void findDefCallsiteGlobalVarEntities();
   void findStructEntity(std::set<Entity*> *&eset); 
   void findDataFieldEntity(std::set<Entity*>*&, bool);
   void findFuncPtrFieldEntity(std::set<Entity*>*&);
   void findFunctionEntity(std::set<Entity*>*&);
   void findCallsiteEntity(std::set<Entity*>*&);
   void findFunctionArgEntity(std::set<Entity*>*&);
   void findMatchingGepForDataField(Identifier *id, std::set<Entity*> &ide);
   void findPointedByFrontiers(std::set<NodeID> &nids2,
                                                  std::map<NodeID, std::set<NodeID>* > &idToFrontierSetMap,
                                                  std::set<NodeID> &all);
   void exploreUsesInSuccessors(const Instruction *inst, const Instruction *orig, const Value *storeUse, const BasicBlock *fb,
                             std::set<const BasicBlock*> &sanbb, std::set<std::string> &fnames, int retval,
                             std::set<const BasicBlock*> &visited);
   const Instruction *followInstrRetValue(const Instruction *inst, const BasicBlock *bb);
   void filterSanitizationBB(const Instruction *inst, const Instruction *orig, std::set<const BasicBlock*> &sanbb,
                                          std::set<std::string> &fnames, int retval);
   void filterSanitizationBB(std::set<NodeID> &sanitizers, std::set<const BasicBlock*> &sanbb,
                                          std::set<std::string> &fnames, std::vector<int> retval);
   void findPointedByFrontiers(NodeID orig, NodeID nid2,
                                                  std::map<NodeID, std::set<NodeID>* > &idToFrontierSetMap,
                                                  std::set<NodeID> &all, std::set<NodeID> &visited);
   bool isDereferencingEdge(const SVFGEdge *edge);
   SVFGNode *getSVFGNode(const Instruction *inst);
   void collectFormalInWoDeref(SVFGNode *node, SVFGNode *orig, std::string ofname, std::set<NodeID> &visited, bool strictValue);
   void summarizeSVFG();
   void dividePerFunc(std::vector<NodeID> &path, std::set<std::string> &pathfnames,
                   std::vector<std::vector<const BasicBlock*> *> &bbpaths);
   bool reaches(NodeID orig, NodeID n1, std::set<NodeID> &n2set, std::set<NodeID> &visited, std::vector<NodeID> &path, 
                std::vector<CallSiteID> &context, int bound, 
                std::vector<std::vector<NodeID>*> &localPaths, 
                std::vector<std::vector<NodeID>*> &compPaths, NodeID &n2, bool strictValue);
   Solution *solveForReaches(Label *label, Identifier *id1, Solution *sol1,
                                          Identifier *id2, Solution *sol2, Solution *sol3, std::vector<int> retval);
   void findSuccessors(NodeID n, std::set<NodeID> &visited, std::set<NodeID> &succs);
   void findPredecessors(NodeID n, std::set<NodeID> &visited, std::set<NodeID> &preds);
   Solution *solveForTaintBothInSeq(Label *label, Identifier *id1, Solution *sol1,
                                          Identifier *id2, Solution *sol2, Identifier *id3, Solution *sol3,
                                          Solution *sol4, std::vector<int> retval);
   Solution *solveForTaintedby(Label *label, Identifier *id1, Solution *sol1, Identifier *id2, Solution *sol2,
                                        Solution *sol3, std::vector<int> retval);
   BinarySolution *solveActualParameterPassedby(Label *label, Identifier *id1, Solution *sol1, Identifier *id2,
                                                          Solution *sol2);
   BinarySolution *solveActualParameterPasses(Label *label, Identifier *id1, Solution *sol1, Identifier *id2, Solution *sol2);
   Solution *solveForActualparameterpassedby(Label *label, Identifier *id1, Solution *sol1, Identifier *id2, Solution *sol2);
   Solution *solveForIndirectCallOf(Label *label, Identifier *id1, Solution *sol1, Identifier *id2, Solution *sol2);
   Solution *solveForPointedBy(Label *label, Identifier *id1, Solution *sol1, Identifier *id2, Solution *sol2);
   Solution *solveForTaint(Label *, Identifier*, Solution*, Identifier*, Solution*, Solution *, std::vector<int>);
   Solution *solveForCallbackDefine(Label *, Identifier*, Solution*, Identifier*, Solution*);
   void solveRegExpForType(RegExpQueryExp *rge, Identifier *id, std::set<Entity*> &ide,
                                         std::map<std::string, Constraint*> &constraints);
   Solution* solveForType(Label*);
   void findCases(const Instruction *inst, const BasicBlock *bb, std::set<BasicBlock*> &trueTargets,
                                std::set<BasicBlock*> &falseTargets, int retval, int &truth, bool &precise);
   ICFGNode *getICFGNode(const BasicBlock *bb);
   void addAllActualParameterNodes();
   ofstream stats;
   long unreach, intermediateUnreach, reach, intermediateReach, numSol, numCachedSol;
   time_t totalSolTime;
   void printResults(Label *label, std::string desc, std::set<std::pair<Entity*,Entity*> > &r);
   void printPath(std::vector<const BasicBlock*> &path);
   void printPath(std::vector<NodeID> &path);
   void printICFGPath(std::vector<NodeID> &path, SVFGNode *node1, SVFGNode *node2);
   bool explainDataFlowViaCF(NodeID n1, NodeID n2);
   bool validCFL(std::vector<NodeID> &path, std::set<const BasicBlock*> &sanbb,
                              std::set<std::string> &sanfunc);
   bool validCFL(std::vector<std::vector<NodeID>*> &compPaths, std::set<const BasicBlock*> &sanbb,
                              std::set<std::string> &sanfunc);
   void findAddrNodes(std::set<NodeID> &ns, std::set<NodeID> &addrnodes,
                  std::map<NodeID, std::set<NodeID> > &am/*, std::map<NodeID, std::set<const Function*> > &funcmap,
                  std::map<const Function*, std::set<NodeID> > &ftonmap*/);
   void findAddrNodes(NodeID o, NodeID n, std::set<NodeID> &visited, std::set<NodeID> &addrnodes,
                  std::map<NodeID, std::set<NodeID> > &am/*, std::map<NodeID, std::set<const Function*> > &funcmap,
                  std::map<const Function*, std::set<NodeID> > &ftonmap*/); 
   bool handleQuery();
   Constraint *generateTaintsDataFieldCallsiteToPrimitiveCons(const char *tname, int field,
                                                                    const char *fname, const char *suffixp);
   Constraint *generateTaintsPtrArgDirectCallToPtrArg(const char *sourcep, const char *sinkp,
                                                                        const char *suffixp, int retval);
   Constraint *generateTernaryForCallsiteWithSourceArgNoCons(const char *fname1, const char *fname2,
                                                                          int argno, const char *suffixp);
   Constraint *generateTaintsPtrArgDirectCallToPrimValArg(const char *sourcep, const char *sinkp,
                                                                        const char *suffixp, int argno);
   Constraint *generateTaintsPtrArgIndirectCallToPrimValArg(const char *datastnamep, const char *sinkp, const char *suffixp);
   Constraint *generateTernaryForCallsiteCons(const char *fname, const char *suffixp);
   Constraint *generateTernaryForCallsiteCons(const char *fname1, const char *fname2, const char *suffixp);
   Constraint *generateTernaryForCallsiteWithSourceTaintCons(const char *fname1, const char *fname2,
                                                                          const char *suffixp, int retval);
   Constraint *generateTernaryForCallsiteWithSourceTaintConsVec(const char *fname1, const char *fname2,
                                                                const char *suffixp, int retval, int retval2, int retval3);
   Constraint *generateTaintsDataFieldCallsiteCons(const char *tname, int field,
                                                                    const char *fname, const char *suffixp);
   Constraint *generateRegExpCons(ETYPE type, const char *descp, std::vector<const char*> *orincludes);
   Constraint *generateRegExpCons(ETYPE type, std::string desc, std::vector<const char*> *orincludes, 
                                  std::string tname, int field, int arg);
   Constraint *generateRelationalCons(UTYPE type, const char *descp, Constraint *i1cons, Constraint *i2cons,
                                                  Constraint *san=NULL, int retval=0);
   Constraint *generateRelationalCons(UTYPE type, std::string desc, Constraint *i1cons, Constraint *i2cons,
                                                  Constraint *san=NULL, int retval=0);
   Constraint *generateTernaryCons(UTYPE type, const char *descp, Constraint *i1cons, Constraint *i2cons,
                                                Constraint *i3cons, Constraint *san, int retval);
   Constraint *generateTernaryConsVec(UTYPE type, const char *descp, Constraint *i1cons, Constraint *i2cons,
                                                Constraint *i3cons, Constraint *san, int retval, int retval2, int retval3);
   Constraint *generateTernaryCons(UTYPE type, std::string desc, Constraint *i1cons, Constraint *i2cons,
                                                Constraint *i3cons, Constraint *san, int retval);
   Constraint *generateTernaryConsVec(UTYPE type, std::string desc, Constraint *i1cons, Constraint *i2cons,
                                                Constraint *i3cons, Constraint *san, int retval, int retval2, int retval3);
  public:
    InfoFlowEngine(SVFG *svfg, ICFG *icfg); 
    Solution *solve(Label *label);
    void runQuery();
    void finalizeStats();
};


#endif
