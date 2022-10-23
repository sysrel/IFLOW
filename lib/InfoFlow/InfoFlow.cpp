#include "InfoFlow/InfoFlow.h"
#include "InfoFlow/FlowAnalyzer.h"
#include "llvm/IR/TypeFinder.h"
#include "MSSA/MemSSA.h"
#include "MemoryModel/PAGNode.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <algorithm>  
#include <iterator>  
#include <time.h>

std::map<NodeID, std::set<std::pair<std::string,NodeID> > *> unreachableBy;
std::map<NodeID, std::set<std::pair<std::string,NodeID> > *> reachableBy;

llvm::cl::opt<std::string> InfoFlowStats("info-flow-stats-file",
                                          llvm::cl::desc("Information flow tracking statistics file name\n"));

llvm::cl::opt<std::string> InfoFlowQueryLabel("info-flow-label",
                                          llvm::cl::desc("Label of the query to be solved (default all queries)\n"));

llvm::cl::opt<unsigned> InfoFlowBound("info-flow-bound",
                                    llvm::cl::desc("The bound for path reachability when performing information flow tracking (default=0 for unbounded)\n"),
                                    llvm::cl::init(0));
                                     
llvm::cl::opt<bool> InfoFlowVerbose("info-flow-verbose",
                                    llvm::cl::desc("Whether detailed info should be provided about the queries (default false)\n"),
                                     llvm::cl::init(true)); 

llvm::cl::opt<bool> InfoFlowQueryVerbose("info-flow-query-verbose",
                                    llvm::cl::desc("Whether detailed info about queries should be written to stats file (default false)\n"),
                                     llvm::cl::init(false));

llvm::cl::opt<bool> FLOWANALYZER("flow-analyzer",
                                  llvm::cl::desc("Flow analyzer\n"),
                                  llvm::cl::init(false));

llvm::cl::opt<bool>
SkipIntraResults("skip-intra-results", 
                 llvm::cl::desc("Skip solutions inside the same function\n"), 
                 llvm::cl::init(false)); 

llvm::cl::opt<bool>
InfoFlowExhaustive("info-flow-exhaustive", 
                    llvm::cl::desc("Whether all pairs of results found regarding reachability (default false)\n"),
                    llvm::cl::init(false));

llvm::cl::opt<bool>
InfoFlowRelaxed("info-flow-relaxed",
                 llvm::cl::desc("Whether data-flow is relaxed, i.e., dereferencing is allowed\n"),
                 llvm::cl::init(false));

llvm::cl::opt<bool>
InfoFlowCtrlSan("info-flow-ctrl-san",
                 llvm::cl::desc("Whether data-flow is sanitized when flows into a conditional branch\n"),
                 llvm::cl::init(false));

llvm::cl::opt<bool>
InfoFlowSummarize("info-flow-summarize",
                  llvm::cl::desc("Whether internal nodes of functions that do not host sinks would be handled via reachability summaries\n"),
                  llvm::cl::init(true));

llvm::cl::opt<bool>
InfoFlowTypeCheckEnforce("info-flow-type-check",
                          llvm::cl::desc("Whether type matching is enforced for matching taintsbothinseq queries\n"),
                          llvm::cl::init(false));

llvm::cl::opt<bool>
InfoFlowExcludeLoops("info-flow-exclude-loops",
                     llvm::cl::desc("Pairs reachable through a loop are excluded from the taintsbothinseq query results\n"),
                     llvm::cl::init(false));

llvm::cl::opt<bool>
InfoFlowIncludeFormals("info-flow-include-formals",
                     llvm::cl::desc("Include formal in nodes\n"),
                     llvm::cl::init(false));

std::map<std::string, bool> enableMap;
std::map<std::string, Label*> labelMap;

llvm::cl::opt<std::string>
InfoFlowQueryType("info-flow-query-type", llvm::cl::desc("Information flow type query\n"));

llvm::cl::list<std::string> InfoFlowQueryOps("info-flow-query-ops", llvm::cl::Positional, llvm::cl::ZeroOrMore);

llvm::cl::opt<std::string> InfoFlowQuerySanVecFile("info-flow-sanvec-file",  
                           llvm::cl::desc("File that lists sanitization values on each line\n"));
// key: FormalParm
// to: set of FormalOut or FormalRet nodes
std::map<NodeID, std::set<NodeID>*> summaryMap;
std::map<NodeID, std::set<NodeID>*> summaryMapRelaxed;
std::map<NodeID, std::set<NodeID>*> summaryMapReverse;
std::map<NodeID, std::set<NodeID>*> summaryMapRelaxedReverse;


extern std::string getInstructionStr(const llvm::Instruction *);

bool matchesSVFGNodeByType(SVFGNode *node, ETYPE type, std::string tname, int field);

const Instruction *getNodeInstruction(SVFGNode *node);

void readNums(const char *fname, std::vector<int> &vals) {
  std::fstream sv(fname, std::fstream::in);
  if (sv.is_open()) {
     std::string line;
     while(std::getline(sv,line)) {
        vals.push_back(std::stoi(line));
     }
     sv.close();
  }
}

std::string findSpecificType(const Instruction *inst) {

   if (const GetElementPtrInst *gep = llvm::dyn_cast<GetElementPtrInst>(inst)) {
      if (gep->getNumOperands() >= 3) {
         if (llvm::Constant *c = llvm::dyn_cast<Constant>(gep->getOperand(2))) {
            int val = c->getUniqueInteger().getLimitedValue();
            return getTypeName(gep->getOperand(0)->getType()) + ":" + std::to_string(val);
         }
      }
   }

   Type *t = inst->getType();
   std::string str = getTypeName(t);
   if (str.find("i8") == std::string::npos) 
      return str;
   else {
      // find field info, if possible
      for(const Use &U : inst->operands())  {
        if (Instruction *ii = llvm::dyn_cast<Instruction>(U)) { 
           if (ii->getOpcode() == Instruction::GetElementPtr && ii->getNumOperands() >= 3) {
             if (llvm::Constant *c = llvm::dyn_cast<Constant>(ii->getOperand(2))) {
                int val = c->getUniqueInteger().getLimitedValue();
                return getTypeName(ii->getOperand(0)->getType()) + ":" + std::to_string(val);  
             }
           } 
        }
      } 
      return str;   
   }
}

std::string findType(SVFG *svfg, SVFGNode *node, SVFGNode *onode, std::set<NodeID> &visited, int depth) {
   if (visited.find(node->getId()) != visited.end())
      return "";
   visited.insert(node->getId());  
   
   if (StmtSVFGNode *stmtNode = SVFUtil::dyn_cast<StmtSVFGNode>(node)) {
      const PAGEdge* edge = stmtNode->getPAGEdge();
      if (SVFUtil::isa<LoadPE>(edge) || SVFUtil::isa<StorePE>(edge) 
          || SVFUtil::isa<GepPE>(edge) || SVFUtil::isa<CopyPE>(edge)) {
         return findSpecificType(stmtNode->getInst());          
      }
   }
   
   if (InfoFlowBound != 0 && depth > InfoFlowBound) 
      return "";
   const SVFGEdge::SVFGEdgeSetTy& inEdges = node->getInEdges();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = inEdges.begin();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = inEdges.end();
   for(; edgeIt != edgeEit; edgeEit++) {
      std::string t = findType(svfg, (*edgeIt)->getSrcNode(), onode, visited, depth + 1);  
      return t;
   }
   return "";
}

std::string findType(SVFG *svfg, SVFGNode *node) {
  std::set<NodeID> visited;
  const Instruction *inst = getNodeInstruction(node);
  if (inst) {
     std::string res = findSpecificType(inst);
     if (res != "")
        return res;
  }
  return findType(svfg, node, node, visited, 1);
}


void InfoFlowEngine::collectFormalInWoDeref(SVFGNode *node, SVFGNode *orig, std::string origname, 
                                            std::set<NodeID> &visited, bool strictFlow) {
     if (visited.find(node->getId()) != visited.end())
        return;
     visited.insert(node->getId());
     if (SVFUtil::isa<FormalINSVFGNode>(node) || SVFUtil::dyn_cast<FormalParmSVFGNode>(node)) {
        FormalINSVFGNode *fin = SVFUtil::dyn_cast<FormalINSVFGNode>(node);
        std::string fname = "";
        if (fin && fin->getFun())
           fname = fin->getFun()->getName().str();
        else { 
           FormalParmSVFGNode* fp = SVFUtil::dyn_cast<FormalParmSVFGNode>(node);
           if (fp && fp->getFun())
              fname = fp->getFun()->getName().str();
        }
        if (origname == fname)  {
           assert(node != orig); 
           if (strictFlow) {
              if (summaryMap.find(node->getId()) == summaryMap.end()) 
                 summaryMap[node->getId()] = new std::set<NodeID>();
              summaryMap[node->getId()]->insert(orig->getId());
              if (summaryMapReverse.find(node->getId()) == summaryMapReverse.end())
                 summaryMapReverse[orig->getId()] = new std::set<NodeID>();
              summaryMapReverse[orig->getId()]->insert(node->getId());              
           }
           else {
              if (summaryMapRelaxed.find(node->getId()) == summaryMapRelaxed.end())
                 summaryMapRelaxed[node->getId()] = new std::set<NodeID>();
              summaryMapRelaxed[node->getId()]->insert(orig->getId());            
              if (summaryMapRelaxedReverse.find(orig->getId()) == summaryMapRelaxedReverse.end())
                 summaryMapRelaxedReverse[orig->getId()] = new std::set<NodeID>();
              summaryMapRelaxedReverse[orig->getId()]->insert(node->getId());
           }
           llvm::errs() << "Summary edge: from " << node->getId() << " to " << orig->getId() << "\n";
           return;
        }
     }
     const SVFGEdge::SVFGEdgeSetTy& inEdges = node->getInEdges();
     SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = inEdges.begin();
     SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = inEdges.end();              
     for(; edgeIt != edgeEit; edgeIt++) {
        const SVFGEdge *edge = *edgeIt;
        // we stop when what is propagated is not preserved!
        if (!strictFlow || !isDereferencingEdge(edge))  { 
           //std::set<NodeID> vt = visited;
           collectFormalInWoDeref(edge->getSrcNode(), orig, origname, visited, strictFlow);
        }
     } 
}

void getSVFGStats(SVFG *g, int &n, int &e) {
  SVFG::SVFGNodeIDToNodeMapTy::iterator it = g->begin();
  SVFG::SVFGNodeIDToNodeMapTy::iterator eit = g->end();
  int N=0, E=0;
  for (; it != eit; ++it) {
      SVFGNode *snode = it->second;
      N++;
      const SVFGEdge::SVFGEdgeSetTy& outEdges = snode->getOutEdges();
      SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = outEdges.begin();
      SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = outEdges.end();
      for (; edgeIt != edgeEit; ++edgeIt) 
          E++;
  }
  n = N;
  e = E;
}

void InfoFlowEngine::summarizeSVFG() {
  int totalformaloutret = 0;
  int totalformaloutretWOutgoing = 0;
  SVFG::SVFGNodeIDToNodeMapTy::iterator it = svfg->begin();
  SVFG::SVFGNodeIDToNodeMapTy::iterator eit = svfg->end();
  for (; it != eit; ++it) {
      SVFGNode *snode = it->second;
      if (SVFUtil::isa<FormalOUTSVFGNode>(snode) || SVFUtil::isa<FormalRetSVFGNode>(snode)) {
         std::string ofname = "";
         if (FormalOUTSVFGNode *fout = SVFUtil::dyn_cast<FormalOUTSVFGNode>(snode)) {
            if (fout->getFun())
               ofname = fout->getFun()->getName().str();
         }
         else if (FormalRetSVFGNode* fr = SVFUtil::dyn_cast<FormalRetSVFGNode>(snode)) {
            if (fr->getFun())
               ofname = fr->getFun()->getName().str();
         }
         totalformaloutret++;
         const SVFGEdge::SVFGEdgeSetTy& outEdges = snode->getOutEdges();
         if (outEdges.size() == 0) continue;
         totalformaloutretWOutgoing++;
         std::set<NodeID> visited, visited2;
         collectFormalInWoDeref(snode, snode, ofname, visited, false);
         collectFormalInWoDeref(snode, snode, ofname, visited2, true);
      }
  }
  llvm::errs() << "Statistics on summarization\n";
  llvm::errs() << "Total number of formal out/ret=" << totalformaloutret << "\n";
  llvm::errs() << "Total number of formal out/ret w outgoing edge=" << totalformaloutretWOutgoing << "\n";
  llvm::errs() << "Total number of formal out/ret w incoming formalin wo deref=" << summaryMap.size() << "\n";
  llvm::errs() << "Total number of formal out/ret w incoming formalin (relaxed)=" << summaryMapRelaxed.size() << "\n";
  stats << "Statistics on summarization\n";
  stats << "Total number of formal out/ret=" << totalformaloutret << "\n";
  stats << "Total number of formal out/ret w outgoing edge=" << totalformaloutretWOutgoing << "\n";
  stats << "Total number of formal out/ret w incoming formalin wo deref=" << summaryMap.size() << "\n";
  stats << "Total number of formal out/ret w incoming formalin (relaxed)=" << summaryMapRelaxed.size() << "\n";
}

std::string getStringForContext(std::vector<CallSiteID> &context) {
  std::string res = "";
  for(int i=0; i<context.size(); i++)
     res += std::to_string(context[i]) + "-";
  return res;
}

extern std::string getTypeName(const llvm::Type *);

std::string toLower(std::string str) {
  std::string res = "";
  for(int i=0; i<str.size(); i++)
     res += std::string(1,tolower(str[i]));
  return res;
}

std::string toUpper(std::string str) {
  std::string res = "";
  for(int i=0; i<str.size(); i++)
     res += std::string(1,toupper(str[i]));
  return res;
}

std::string getBaseTypeName(const llvm::Type *type) {
  if (type->isPointerTy()) {
     return getBaseTypeName(type->getPointerElementType());
  }
  else return getTypeName(type);
}

extern bool usesOf(const Instruction *i1, const Instruction *i2, const BasicBlock *bb);

const Instruction *getNodeInstruction(SVFGNode *node) {
  if (StmtSVFGNode* stmtNode = SVFUtil::dyn_cast<StmtSVFGNode>(node)) 
     return stmtNode->getInst();
  if (ActualParmSVFGNode* ap = SVFUtil::dyn_cast<ActualParmSVFGNode>(node))
     return ap->getCallSite().getInstruction();
  if (ActualRetSVFGNode* ar = SVFUtil::dyn_cast<ActualRetSVFGNode>(node)) 
     return ar->getCallSite().getInstruction(); 
  if (SVFUtil::isa<ActualINSVFGNode>(node)) {
     ActualINSVFGNode *ain = SVFUtil::dyn_cast<ActualINSVFGNode>(node);
     CallSite cs = ain->getCallSite();
     return cs.getInstruction();
  }
  if (SVFUtil::isa<ActualOUTSVFGNode>(node)) {
     ActualOUTSVFGNode *ain = SVFUtil::dyn_cast<ActualOUTSVFGNode>(node);
     CallSite cs = ain->getCallSite();
     return cs.getInstruction();
  }
  if (MSSAPHISVFGNode* mphi = SVFUtil::dyn_cast<MSSAPHISVFGNode>(node)) {
     return &mphi->getBB()->back();
  }
  return NULL;
}

ICFGNode *InfoFlowEngine::getICFGNode(const BasicBlock *bb) {
  for(llvm::DenseMap<NodeID, ICFGNode *>::iterator it = icfg->begin(); it != icfg->end(); it++) {
     if ((*it).second->getBB() == bb) 
        return (*it).second;
  }
  return NULL;
}

std::string prettyPrint(const Instruction *inst) {
  if (!inst) return "";
  std::string res = "";
  std::string loc = SVFUtil::getSourceLoc(inst);
  std::string suffix = "";
  if (loc == "")
     suffix += " in " + inst->getParent()->getParent()->getName().str() + " "; 
  else 
     suffix = " line no " + loc + " ";
  res += getInstructionStr(inst) + suffix;

  return res;
}

std::string prettyPrint(const ICFGNode *node) {
  std::string res = "";
  if (const IntraBlockNode *in = SVFUtil::dyn_cast<IntraBlockNode>(node)) {
      res += "(STMT)" + getInstructionStr(in->getInst()) + " " + 
              SVFUtil::getSourceLoc(in->getInst()) + " in " + in->getInst()->getParent()->getParent()->getName().str();        
  }
  else if (const FunEntryBlockNode *fe = SVFUtil::dyn_cast<FunEntryBlockNode>(node)) {
     assert(fe->getFun());
     res += "(" + fe->getFun()->getName().str() + " Entry)";
  }
  else if (const FunExitBlockNode *fe = SVFUtil::dyn_cast<FunExitBlockNode>(node)) {
     assert(fe->getFun());
     res += "(" + fe->getFun()->getName().str() + " Exit)";
  }
  else if (const CallBlockNode *fe = SVFUtil::dyn_cast<CallBlockNode>(node)) {
     res += "(" + getInstructionStr(fe->getCallSite().getInstruction()) + " Callsite)";
  }
  else if (const RetBlockNode *fe = SVFUtil::dyn_cast<RetBlockNode>(node)) {
     res += "(" + getInstructionStr(fe->getCallSite().getInstruction()) + " Ret)";
  }
  return res;
}

std::string prettyPrint(SVFGNode *node) {
  std::string res = "";
  if (!node) return "";
  if (StmtSVFGNode* stmtNode = SVFUtil::dyn_cast<StmtSVFGNode>(node)) {
     if (stmtNode->getInst()) {
        res += "(STMT)" + prettyPrint(stmtNode->getInst());
        //res += "(STMT)" + getInstructionStr(stmtNode->getInst()) + " " + 
          //     SVFUtil::getSourceLoc(stmtNode->getInst());
     }
     else if (stmtNode->getPAGDstNode()->hasValue()) {
        res += "(STMTDST) " + SVFUtil::getSourceLoc(stmtNode->getPAGDstNode()->getValue());
     }
  }
  else if (PHISVFGNode* tphi = SVFUtil::dyn_cast<PHISVFGNode>(node)) {
     res += "(PHISTMTRES) " + SVFUtil::getSourceLoc(tphi->getRes()->getValue());
  }
    if (ActualParmSVFGNode* ap = SVFUtil::dyn_cast<ActualParmSVFGNode>(node)) {
     res += "(APAR) " + prettyPrint(ap->getCallSite().getInstruction());
    // res += "(APAR) " + getInstructionStr(ap->getCallSite().getInstruction())
      //      + SVFUtil::getSourceLoc(ap->getCallSite().getInstruction());
  }
  else if (FormalParmSVFGNode* fp = SVFUtil::dyn_cast<FormalParmSVFGNode>(node)) {
     res += "(FPARM) " + fp->getFun()->getName().str(); 
  }
  else if (ActualRetSVFGNode* ar = SVFUtil::dyn_cast<ActualRetSVFGNode>(node)) { 
     res += "(ARET) " + prettyPrint(ar->getCallSite().getInstruction()); 
     //res += "(ARET) " + getInstructionStr(ar->getCallSite().getInstruction()) + 
       //     " " + SVFUtil::getSourceLoc(ar->getCallSite().getInstruction());
  }
  else if (FormalRetSVFGNode* fr = SVFUtil::dyn_cast<FormalRetSVFGNode>(node)) {
     res +=  "(FRET) " + fr->getFun()->getName().str(); 
  }
  else if (SVFUtil::isa<FormalINSVFGNode>(node)) {
     FormalINSVFGNode *fin = SVFUtil::dyn_cast<FormalINSVFGNode>(node);
     if (fin->getFun())
        res += "(FIN) " + fin->getFun()->getName().str();
     else res += "UNKNOWN(FIN)";
  }
  else if (SVFUtil::isa<FormalOUTSVFGNode>(node)) {
     FormalOUTSVFGNode *fout = SVFUtil::dyn_cast<FormalOUTSVFGNode>(node);
     if (fout->getFun())
        res +=  "(FOUT) " + fout->getFun()->getName().str() + "(FOUT)";
     else res += "UNKNOWN(FOUT)";
  }
  else if (SVFUtil::isa<ActualINSVFGNode>(node)) {
     ActualINSVFGNode *ain = SVFUtil::dyn_cast<ActualINSVFGNode>(node);
     CallSite cs = ain->getCallSite();
     if (cs.getInstruction()) {
        res += "(AIN) " + getInstructionStr(cs.getInstruction()) + 
               " " + SVFUtil::getSourceLoc(cs.getInstruction());
        res += " within " + cs.getCaller()->getName().str();
     }
     else res += "UNKNOWN callsite (AIN)";
  }
  else if (SVFUtil::isa<ActualOUTSVFGNode>(node)) {
     ActualOUTSVFGNode *ain = SVFUtil::dyn_cast<ActualOUTSVFGNode>(node);
     CallSite cs = ain->getCallSite();
     if (cs.getInstruction()) {
        res += "(AOUT) " + getInstructionStr(cs.getInstruction()) + 
               " " + SVFUtil::getSourceLoc(cs.getInstruction());  
        res += " within " + cs.getCaller()->getName().str();
     }
     else res += "UNKNOWN callsite (AOUT)";
  }
  else if (MSSAPHISVFGNode* mphi = SVFUtil::dyn_cast<MSSAPHISVFGNode>(node)) {
     res += "(PHI) in " + ((Function*)mphi->getBB()->getParent())->getName().str() +
            " " + prettyPrint(&mphi->getBB()->back());
     //res += "(PHI) in " + ((Function*)mphi->getBB()->getParent())->getName().str() +
       //     " " + getInstructionStr(&mphi->getBB()->back()) + " " + SVFUtil::getSourceLoc(&mphi->getBB()->back());
  }
  else if (SVFUtil::isa<NullPtrSVFGNode>(node)) {
     res += "NullPtr";
  }
  res += "(ID: " + std::to_string(node->getId()) + ")";
  return res;
}

InfoFlowEngine::InfoFlowEngine(SVFG *svfg, ICFG *icfg) {
   this->svfg = svfg;
   this->icfg = icfg;
   addAllActualParameterNodes();
   if (InfoFlowStats != "")
      stats.open(InfoFlowStats.c_str());
   else
      stats.open("infoflowstats.txt");
   unreach = 0;
   intermediateUnreach = 0; 
   reach = 0;
   intermediateReach = 0;
   numSol = 0;
   numCachedSol = 0;
   totalSolTime = 0;
} 

// s3 = s1 INTERSECT s2
void findIntersection(std::set<Entity*> &s1, std::set<Entity*> &s2, std::set<Entity*> &s3) {
   if (s1.empty() || s2.empty())
      return;
   for(auto es1: s1) {
      for(auto es2: s2) {
         if (es1->matches(es2)) {
            s3.insert(es1);
            continue;
         }
      }
   }
}


SVFGNode *InfoFlowEngine::getSVFGNode(const Instruction *inst) {
  SVFG::SVFGNodeIDToNodeMapTy::iterator it = svfg->begin();
  SVFG::SVFGNodeIDToNodeMapTy::iterator eit = svfg->end();
  for (; it != eit; ++it) {
      SVFGNode *snode = it->second;
      if (inst == getNodeInstruction(snode))
         return snode;
  }
  return NULL;
}

void InfoFlowEngine::findMatchingGepForDataField(Identifier *id, std::set<Entity*> &ide) {
  assert(id->getTypeName() != "");
  SVFG::SVFGNodeIDToNodeMapTy::iterator it = svfg->begin();
  SVFG::SVFGNodeIDToNodeMapTy::iterator eit = svfg->end();
  for (; it != eit; ++it) {
      SVFGNode *snode = it->second;
      if (matchesSVFGNodeByType(snode, fieldAddrExprT, id->getTypeName(), id->getField())) {
         const Instruction *inst = getNodeInstruction(snode);
         llvm::errs() << "Matched type=" << id->getTypeName() << " field=" << id->getField() 
         << " to node " << snode->getId() << " " << (*inst) << " " << prettyPrint(snode) << "\n";
         Entity *e = Entity::create(fieldAddrExprT, snode);
         ide.insert(e);
         // This is needed as SVF may fail to properly connect two successor gep nodes in svfg
         const Instruction *isuc = inst->getNextNonDebugInstruction();
         if (isuc && isuc->getOpcode() == Instruction::GetElementPtr) {
            if (const GetElementPtrInst *gep = llvm::dyn_cast<GetElementPtrInst>(isuc)) {
                // to do : double check the use relationship
                SVFGNode *sucn = getSVFGNode(gep);
                Entity *e2 = Entity::create(fieldAddrExprT, sucn);
                ide.insert(e2);
            }
         }
      }
  }
} 


// find the struct type

void InfoFlowEngine::findStructEntity(std::set<Entity*> *&eset) {
  eset = &rawdataentities;
  if (!rawdataentities.empty()) 
     return;
  MemSSA *mssa = svfg->getMSSA(); 
  PointerAnalysis *pta = mssa->getPTA();
  SVFModule svfModule = pta->getModule();
  llvm::DataLayout * dataLayout = new DataLayout(svfModule.getMainLLVMModule());
  llvm::TypeFinder StructTypes;
  const llvm::Module &m = *svfModule.getMainLLVMModule();
  StructTypes.run(m, true);
  for(auto *STy : StructTypes) {
     DataEntity *de = new DataEntity(NULL, STy);
     rawdataentities.insert(de);
  }
}

bool isFunctionPointer(const llvm::Type *t) {
  if (t->isPointerTy()) {
      llvm::Type *et = t->getPointerElementType();
      return et->isFunctionTy();
  }
  return false;
}

void InfoFlowEngine::findDataFieldEntity(std::set<Entity*> *&eset, bool pointer) {
  if (pointer) 
     eset = &rawpointerfieldentities;
  else 
     eset = &rawprimitivefieldentities;
  if (pointer && !rawpointerfieldentities.empty()) {
     return;
  }
  if (!pointer && !rawprimitivefieldentities.empty()) {
     return;
  }
  MemSSA *mssa = svfg->getMSSA();
  PointerAnalysis *pta = mssa->getPTA();
  SVFModule svfModule = pta->getModule();
  llvm::DataLayout * dataLayout = new DataLayout(svfModule.getMainLLVMModule());
  llvm::TypeFinder StructTypes;
  const llvm::Module &m = *svfModule.getMainLLVMModule();
  StructTypes.run(m, true);
  for(auto *STy : StructTypes) {
     for(int i=0; i<STy->getNumElements(); i++) {
        llvm::Type *et = STy->getElementType(i);
        bool matchespointer = (et->isPointerTy() && !isFunctionPointer(et));
        bool matchesnonpointer = (!et->isPointerTy());
        if (pointer) { 
           FieldEntity *fe = new FieldEntity(NULL, STy, i, STy->getElementType(i));
           rawpointerfieldentities.insert(fe);
        }            
        if (!pointer && matchesnonpointer) {
           FieldEntity *fe = new FieldEntity(NULL, STy, i, STy->getElementType(i));
           rawprimitivefieldentities.insert(fe);
        }        
     }
  }  
}

void InfoFlowEngine::findFuncPtrFieldEntity(std::set<Entity*> *&eset) {
  eset = &rawfuncptrfieldentities;
  if (!rawfuncptrfieldentities.empty()) {
     return;
  }
  MemSSA *mssa = svfg->getMSSA();
  PointerAnalysis *pta = mssa->getPTA();
  SVFModule svfModule = pta->getModule();
  llvm::DataLayout * dataLayout = new DataLayout(svfModule.getMainLLVMModule());
  llvm::TypeFinder StructTypes;
  const llvm::Module &m = *svfModule.getMainLLVMModule();
  StructTypes.run(m, true);
  for(auto *STy : StructTypes) {
     for(int i=0; i<STy->getNumElements(); i++) {
        llvm::Type *et = STy->getElementType(i);
        if (isFunctionPointer(et)) {
           //llvm::errs() << "Function pointer field type " << getTypeName(et) << " in struct " << getTypeName(STy) << "\n";
           FuncPtrFieldEntity *fe = new FuncPtrFieldEntity(NULL, STy, i, STy->getElementType(i));
           rawfuncptrfieldentities.insert(fe);
        }
     }
  }
}

void InfoFlowEngine::findFunctionEntity(std::set<Entity*> *&eset) {
  eset = &rawfunctionentities;
  if (!rawfunctionentities.empty()) {
     return;
  }
  MemSSA *mssa = svfg->getMSSA();
  PointerAnalysis *pta = mssa->getPTA();
  SVFModule svfModule = pta->getModule();
  llvm::Module *m = svfModule.getMainLLVMModule();
  for(llvm::Module::iterator it = m->begin(); it != m->end(); it++) {
     FunctionEntity *fe = new FunctionEntity(NULL, &(*it));
     rawfunctionentities.insert(fe);
  }  
}

void InfoFlowEngine::findCallsiteEntity(std::set<Entity*> *&eset) {
  eset = &rawcallsiteentities;
  if (!rawcallsiteentities.empty())
     return;
  MemSSA *mssa = svfg->getMSSA();
  PointerAnalysis *pta = mssa->getPTA();
  SVFModule svfModule = pta->getModule();
  llvm::Module *m = svfModule.getMainLLVMModule();
  for(llvm::Module::iterator it = m->begin(); it != m->end(); it++) {
      for(llvm::Function::iterator bit = (*it).begin(); bit != (*it).end(); bit++) {
         for(llvm::BasicBlock::iterator iit = (*bit).begin(); iit != (*bit).end(); iit++) {
            const llvm::Instruction  &ii = *iit;
             if (ii.getOpcode() == llvm::Instruction::Invoke || 
                 ii.getOpcode() == llvm::Instruction::Call) { 
                 CallsiteEntity *cs = new CallsiteEntity(NULL, new llvm::CallSite(&(*iit)));
                 rawcallsiteentities.insert(cs);
             }                 
         }
      }
  }    
}


void InfoFlowEngine::findGlobalVarEntities(std::set<Entity*> *&eset) {
  eset = &rawglobalvariables;
  if (!rawglobalvariables.empty())
      return;
  MemSSA *mssa = svfg->getMSSA();
  PointerAnalysis *pta = mssa->getPTA();
  SVFModule svfModule = pta->getModule();
  for(SVFModule::global_iterator I = svfModule.global_begin(), E =
                svfModule.global_end(); I != E; ++I) {
     llvm::GlobalVariable *gv = *I;
     if (!gv->isConstant()) {
        llvm::Type *gt = gv->getType();                
        GlobalVarEntity *gve = new GlobalVarEntity(gv->getName(), NULL, gv, gv->getType()); 
        rawglobalvariables.insert(gve);
        if (isFunctionPointer(gt)) {
           rawfuncptrglobalvariables.insert(gve);    
        }
        else if (gt->isPointerTy()) {
           rawpointerglobalvariables.insert(gve);
        }
        else if (gt->isStructTy()) {
           rawstructglobalvariables.insert(gve);
        } 
        else {
           rawprimitiveglobalvariables.insert(gve);           
        }
     } 
  }
}

void InfoFlowEngine::findUseGlobalVarEntities(std::set<Entity*> *&eset) {
   eset = &rawuseglobalvariables;
   if (!rawuseglobalvariables.empty())
      return;
   findUseLoadGlobalVarEntities();
   findUseCallsiteGlobalVarEntities();
}

void InfoFlowEngine::findUseLoadGlobalVarEntities() {
   PAG *pag = svfg->getPAG();
   MemSSA *mssa = svfg->getMSSA();
   // load mus
   llvm::DenseMap<const LoadPE*,  std::set<MemSSA::MU*> > &lm = mssa->getLoadToMUSetMap();
   for(auto lmum : lm) {
      const LoadPE* lpe = lmum.first;
      StmtVFGNode *svfgnode = svfg->getStmtVFGNode(lpe);
      std::set<MemSSA::MU*> &ms = lmum.second;
      MRGenerator::MRSet &mrs = mssa->getMRGenerator()->getLoadMRSet(lpe);
      for(auto mr : mrs) {
         const PointsTo &pts = mr->getPointsTo();
         //    llvm::errs() << mr->dumpStr() << "\n";
        for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
           NodeID ptd = *ii;
           const PAGNode* node = pag->getPAGNode(ptd); 
           if (SVFUtil::isa<ObjPN>(node)) {
              const ObjPN* obj = SVFUtil::cast<ObjPN>(node);
              const MemObj *mo = obj->getMemObj();
              if (!mo->isFunction() && mo->isGlobalObj()) {
                     // ok, we can record it as globalvar use 
                 UseGlobalVarEntity *uge = new UseGlobalVarEntity(mo->getRefVal()->getName(), 
                                                      svfgnode, mo->getRefVal(), mo->getType());
                 rawuseglobalvariables.insert(uge);
              }
           }
        } 
      }
   }
}

// one problem is that there isn't a related SVFG node for the callsite as 
// the arguments are assigned SVFG nodes 
void InfoFlowEngine::findUseCallsiteGlobalVarEntities() {
   //  ActualINSVFGNode
   PAG *pag = svfg->getPAG();
   const PointerAnalysis::CallSiteSet &csset = pag->getCallSiteSet();
   for(auto cs : csset) {
       NodeBS &ains = svfg->getActualINSVFGNodes(cs);
       for(auto ain : ains) {
          ActualINSVFGNode *svfgnode = (ActualINSVFGNode*)svfg->getSVFGNode(ain); 
          const PointsTo &pts = svfgnode->getPointsTo();
          //llvm::errs() << mr->dumpStr() << "\n";
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
             const PAGNode* node = pag->getPAGNode(ptd);
             if (SVFUtil::isa<ObjPN>(node)) {
                const ObjPN* obj = SVFUtil::cast<ObjPN>(node);
                const MemObj *mo = obj->getMemObj();
                if (!mo->isFunction() && mo->isGlobalObj()) {
                   // ok, we can record it as globalvar use
                   UseGlobalVarEntity *uge = new UseGlobalVarEntity(mo->getRefVal()->getName().str(),
                                                     svfgnode, mo->getRefVal(), mo->getType());
                   rawuseglobalvariables.insert(uge);
                }
             }
          }
       }
    }
  MemSSA *mssa = svfg->getMSSA();
  PointerAnalysis *pta = mssa->getPTA();
  SVFModule svfModule = pta->getModule();
  llvm::Module *m = svfModule.getMainLLVMModule();
  for(llvm::Module::iterator it = m->begin(); it != m->end(); it++) {
     // FormalOUTSVFGNode
      NodeBS &fouts = svfg->getFormalOUTSVFGNodes(&(*it));
       for(auto fout : fouts) {
          FormalOUTSVFGNode *svfgnode = (FormalOUTSVFGNode*)svfg->getSVFGNode(fout);
          const PointsTo &pts = svfgnode->getPointsTo();
          //llvm::errs() << mr->dumpStr() << "\n";
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
             const PAGNode* node = pag->getPAGNode(ptd);
             if (SVFUtil::isa<ObjPN>(node)) {
                const ObjPN* obj = SVFUtil::cast<ObjPN>(node);
                const MemObj *mo = obj->getMemObj();
                if (!mo->isFunction() && mo->isGlobalObj()) {
                   // ok, we can record it as globalvar use
                   UseGlobalVarEntity *uge = new UseGlobalVarEntity(mo->getRefVal()->getName().str(),
                                                     svfgnode, mo->getRefVal(), mo->getType());
                   rawuseglobalvariables.insert(uge);
                }
             }
          }
       }
   }
   /*MemSSA *mssa = svfg->getMSSA();
   std::map<llvm::CallSite, std::set<MemSSA::MU*> > &sm = mssa->getCallSiteToMuSetMap();
   for(auto smum : sm) {
      CallSite cs = smum.first;
      std::set<MemSSA::MU*> &ms = smum.second;
      MRGenerator::MRSet &mrs = mssa->getMRGenerator()->getCallSiteRefMRSet(cs);
      for(auto mr : mrs) {
          const PointsTo &pts = mr->getPointsTo();
          llvm::errs() << mr->dumpStr() << "\n";
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
             const PAGNode* node = pag->getPAGNode(ptd);
             if (SVFUtil::isa<ObjPN>(node)) {
                const ObjPN* obj = SVFUtil::cast<ObjPN>(node);
                const MemObj *mo = obj->getMemObj();
                if (!mo->isFunction() && mo->isGlobalObj()) {
                   UseGlobalVarEntity *uge = new UseGlobalVarEntity(mo->getRefVal()->getName(),
                                                      NULL, mo->getRefVal(), mo->getType());
                   rawuseglobalvariables.insert(uge);
                }
             }
          }
      }
   }
   */
}


void InfoFlowEngine::findDefStoreGlobalVarEntities() {
   PAG *pag = svfg->getPAG();
   MemSSA *mssa = svfg->getMSSA();
   // store chi
   llvm::DenseMap<const StorePE*,  std::set<MemSSA::CHI*> > &sm = mssa->getStoreToChiSetMap();
   for(auto smum : sm) {
      const StorePE* spe = smum.first;
      SVFGNode *svfgnode = svfg->getStmtVFGNode(spe);
      std::set<MemSSA::CHI*> &ms = smum.second;
      MRGenerator::MRSet &mrs = mssa->getMRGenerator()->getStoreMRSet(spe);
      for(auto mr : mrs) {
         const PointsTo &pts = mr->getPointsTo();
         //    llvm::errs() << mr->dumpStr() << "\n";
        for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
           NodeID ptd = *ii;
           const PAGNode* node = pag->getPAGNode(ptd);
           if (SVFUtil::isa<ObjPN>(node)) {
              const ObjPN* obj = SVFUtil::cast<ObjPN>(node);
              const MemObj *mo = obj->getMemObj();
              if (!mo->isFunction() && mo->isGlobalObj()) {
                     // ok, we can record it as globalvar use
                 DefGlobalVarEntity *dge = new DefGlobalVarEntity(mo->getRefVal()->getName().str(),
                                                      svfgnode, mo->getRefVal(), mo->getType());
                 rawdefglobalvariables.insert(dge);
              }
           }
        }
      }
   }
}


void InfoFlowEngine::findDefCallsiteGlobalVarEntities() {
   PAG *pag = svfg->getPAG();
   const PointerAnalysis::CallSiteSet &csset = pag->getCallSiteSet();
   for(auto cs : csset) {
       NodeBS &ains = svfg->getActualOUTSVFGNodes(cs);
       for(auto ain : ains) {
          ActualOUTSVFGNode *svfgnode = (ActualOUTSVFGNode*)svfg->getSVFGNode(ain);
          const PointsTo &pts = svfgnode->getPointsTo();
          //llvm::errs() << mr->dumpStr() << "\n";
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
             const PAGNode* node = pag->getPAGNode(ptd);
             if (SVFUtil::isa<ObjPN>(node)) {
                const ObjPN* obj = SVFUtil::cast<ObjPN>(node);
                const MemObj *mo = obj->getMemObj();
                if (!mo->isFunction() && mo->isGlobalObj()) {
                   // ok, we can record it as globalvar use
                   DefGlobalVarEntity *dge = new DefGlobalVarEntity(mo->getRefVal()->getName().str(),
                                                     svfgnode, mo->getRefVal(), mo->getType());
                   rawdefglobalvariables.insert(dge);
                }
             }
          }
       }
    }
  MemSSA *mssa = svfg->getMSSA();
  PointerAnalysis *pta = mssa->getPTA();
  SVFModule svfModule = pta->getModule();
  llvm::Module *m = svfModule.getMainLLVMModule();
  for(llvm::Module::iterator it = m->begin(); it != m->end(); it++) {
     // FormalOUTSVFGNode
      NodeBS &fouts = svfg->getFormalINSVFGNodes(&(*it));
       for(auto fout : fouts) {
          FormalINSVFGNode *svfgnode = (FormalINSVFGNode*)svfg->getSVFGNode(fout);
          const PointsTo &pts = svfgnode->getPointsTo();
          //llvm::errs() << mr->dumpStr() << "\n";
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
             const PAGNode* node = pag->getPAGNode(ptd);
             if (SVFUtil::isa<ObjPN>(node)) {
                const ObjPN* obj = SVFUtil::cast<ObjPN>(node);
                const MemObj *mo = obj->getMemObj();
                if (!mo->isFunction() && mo->isGlobalObj()) {
                   // ok, we can record it as globalvar use
                   DefGlobalVarEntity *uge = new DefGlobalVarEntity(mo->getRefVal()->getName().str(),
                                                     svfgnode, mo->getRefVal(), mo->getType());
                   rawdefglobalvariables.insert(uge);
                }
             }
          }
       }
   }
}

 void InfoFlowEngine::findDefGlobalVarEntities(std::set<Entity*> *&eset) {
   eset = &rawdefglobalvariables;
   if (!rawdefglobalvariables.empty())
      return;
   findDefStoreGlobalVarEntities();
   findDefCallsiteGlobalVarEntities();
}


void InfoFlowEngine::findFunctionArgEntity(std::set<Entity*>*&eset) {
  eset = &rawfunctionargentities;
  if (!rawfunctionargentities.empty())
     return;
  PAG *pag = svfg->getPAG();
  MemSSA *mssa = svfg->getMSSA();
  PointerAnalysis *pta = mssa->getPTA();
  SVFModule svfModule = pta->getModule();
  llvm::Module *m = svfModule.getMainLLVMModule();
  SVFG::SVFGNodeIDToNodeMapTy::iterator it = svfg->begin();
  SVFG::SVFGNodeIDToNodeMapTy::iterator eit = svfg->end();
  for (; it != eit; ++it) {
      if (ActualParmSVFGNode* ap = SVFUtil::dyn_cast<ActualParmSVFGNode>(it->second)) {
         const PAGNode *pn = ap->getParam();
         if (!pn) continue;
         if ((pn->getNodeKind() != PAGNode::DummyValNode) && (pn->getNodeKind() != PAGNode::DummyObjNode)) {
            //llvm::errs() << "Function arg node id: " << it->first << "\n";
            //llvm::errs() << "Nodekind=" << pn->getNodeKind() << " DummyValNode " << PAGNode::DummyValNode 
            //             << " DummyObjNode " << PAGNode::DummyObjNode << "\n";
            const Value *pv = pn->getValue();
            if (pv) {
               //llvm::errs() << "value " << (*pv) << "\n";
               FunctionArgEntity *fae = new FunctionArgEntity(it->second, pn->getFunction(), pv->getType());
               rawfunctionargentities.insert(fae);         
            }
        } 
      }
  }

  
  for(llvm::Module::iterator it = m->begin(); it != m->end(); it++) {
      NodeBS &fins = svfg->getFormalINSVFGNodes(&(*it));
      for(auto fin : fins) {
          FormalINSVFGNode *svfgnode = (FormalINSVFGNode*)svfg->getSVFGNode(fin);  
          const PointsTo &pts = svfgnode->getPointsTo();
          const PAGNode* node = NULL;
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
              node = pag->getPAGNode(ptd);
              if (!node) continue;
              if ((node->getNodeKind() != PAGNode::DummyValNode) && (node->getNodeKind() != PAGNode::DummyObjNode)) {
                 FunctionArgEntity *fae = new FunctionArgEntity(svfgnode, node->getFunction(), node->getValue()->getType());
                 rawfunctionargentities.insert(fae);
              }
          }
      }
      // Redundant
      /*NodeBS &fouts = svfg->getFormalOUTSVFGNodes(&(*it));
      for(auto fout : fouts) {
          FormalOUTSVFGNode *svfgnode = (FormalOUTSVFGNode*)svfg->getSVFGNode(fout);  
          //const MemRegion* mr = svfgnode->getMR();
          const PointsTo &pts = svfgnode->getPointsTo();
          const PAGNode* node = NULL;
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
              node = pag->getPAGNode(ptd);
              if (!node) continue;
              if ((node->getNodeKind() != PAGNode::DummyValNode) && (node->getNodeKind() != PAGNode::DummyObjNode)) {
                 FunctionArgEntity *fae = new FunctionArgEntity(svfgnode, node->getFunction(), node->getValue()->getType());
                 rawfunctionargentities.insert(fae);
              }
          }
      }*/
  }
  //svfg->dump(std::string("mysvfg.txt"));
  /*for(PAG::CSToArgsListMap::iterator cit = pag->getCallSiteArgsMap().begin(),
            ceit = pag->getCallSiteArgsMap().end(); cit!=ceit; ++cit) {
        std::list<const PAGNode*>& arglist = cit->second;
        for(std::list<const PAGNode*>::iterator lit = arglist.begin(); lit != arglist.end(); lit++) {
           if (svfg->PAGNodeToActualParmMap.find(std::make_pair((*lit)->getId(),cit->first)) != svfg->PAGNodeToActualParmMap.end()) {
              const SVFGNode* sn = svfg->getActualParmVFGNode(*lit,cit->first);
              if (!sn) continue;
              llvm::errs() << "Node id: " << sn->getId() << "\n";
              FunctionArgEntity *fae = new FunctionArgEntity(svfg->getSVFGNode(sn->getId()), NULL, (*lit)->getValue()->getType());
              rawfunctionargentities.insert(fae);
           }
        }
  }*/
  const PointerAnalysis::CallSiteSet &csset = pag->getCallSiteSet();
  for(auto cs : csset) {
      NodeBS &ains = svfg->getActualINSVFGNodes(cs);
      for(auto ain : ains) {
          ActualINSVFGNode *svfgnode = (ActualINSVFGNode*)svfg->getSVFGNode(ain);
          const PointsTo &pts = svfgnode->getPointsTo();
          const PAGNode* node = NULL;
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
              node = pag->getPAGNode(ptd);
              if (!node) continue;
              if ((node->getNodeKind() != PAGNode::DummyValNode) && (node->getNodeKind() != PAGNode::DummyObjNode)) {
                 FunctionArgEntity *fae = new FunctionArgEntity(svfgnode, node->getFunction(), node->getValue()->getType());
                 rawfunctionargentities.insert(fae);
              }
          }
      }
      // Redundant
      /*
      NodeBS &aouts = svfg->getActualOUTSVFGNodes(cs);
      for(auto aout : aouts) {
          ActualOUTSVFGNode *svfgnode = (ActualOUTSVFGNode*)svfg->getSVFGNode(aout);
          const PointsTo &pts = svfgnode->getPointsTo();
          const PAGNode* node = NULL;
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
              node = pag->getPAGNode(ptd);
              if (!node) continue;
              if ((node->getNodeKind() != PAGNode::DummyValNode) && (node->getNodeKind() != PAGNode::DummyObjNode)) {
                 FunctionArgEntity *fae = new FunctionArgEntity(svfgnode, node->getFunction(), node->getValue()->getType());
                 rawfunctionargentities.insert(fae);
              }
          }
      }*/
  }
}

bool matchArgNo(const SVFGNode *node, int arg) {
   const SVFGEdge::SVFGEdgeSetTy& outEdges = node->getOutEdges();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = outEdges.begin();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = outEdges.end();
   for (; edgeIt != edgeEit; ++edgeIt) {
       const SVFGEdge *edge = *edgeIt;
       SVFGNode *sn = edge->getDstNode(); 
       if (PHISVFGNode* mphi = SVFUtil::dyn_cast<PHISVFGNode>(sn)) {
          if (const Argument *argum = SVFUtil::dyn_cast<Argument>(mphi->getRes()->getValue())) {
             if (argum->getArgNo() == arg) {
                 llvm::errs() << "Argument " << arg << " matching node " << node->getId() << "\n"; 
                 return true;
             }
          }
       }
   }
   return false;
}


BinarySolution *InfoFlowEngine::solveActualParameterPassedby(Label *label, Identifier *id1, Solution *sol1, Identifier *id2,
                                                          Solution *sol2) {
  PAG *pag = svfg->getPAG();
  std::set<std::pair<Entity*, Entity*> > results;
  std::set<Entity*> res = sol2->getResults();
  std::set<std::tuple<NodeID, const Function*, const Type *> > uniqRes; 
  for(auto ent : res) 
  {
     CallSite cs = *ent->getCallsite();
     if (pag->getCallSiteArgsMap().find(cs) != pag->getCallSiteArgsMap().end()) 
     {
         std::list<const PAGNode*>& arglist = pag->getCallSiteArgsMap()[cs];
         for(std::list<const PAGNode*>::iterator lit = arglist.begin(); lit != arglist.end(); lit++) 
         {
           if (svfg->PAGNodeToActualParmMap.find(std::make_pair((*lit)->getId(),cs)) !=
                   svfg->PAGNodeToActualParmMap.end()) 
           {
              const SVFGNode* sn = svfg->getActualParmVFGNode(*lit,cs);
              if (!sn) continue;
              if (const ActualParmSVFGNode* ap = SVFUtil::dyn_cast<ActualParmSVFGNode>(sn)) 
              {
                 //llvm::errs() << "actual par: " << sn->getId() << "\n";
                 const PAGNode *pn = ap->getParam();
                 if (!pn) continue;
                 if ((pn->getNodeKind() != PAGNode::DummyValNode) && (pn->getNodeKind() != PAGNode::DummyObjNode)) 
                 {
                    const Value *pv = pn->getValue();
                    if (pv) 
                    {
                      Type *type = pv->getType();
                      //llvm::errs() << "id name=" << id1->getName() << " type=" << id1->getType() << " type=" << getTypeName(type) << "\n";
                      if (id1->getType() == funcptrFuncArgT && !isFunctionPointer(type)) {
                         //llvm::errs() << "Skipping non func pointer arg type \n";
                         continue;
                      }
                      if (id1->getType() == pointerFuncArgT && !type->isPointerTy()) {
                         //llvm::errs() << "Skipping non pointer arg type \n";
                         continue;
                      }
                      if (id1->getType() == structFuncArgT && !type->isStructTy()) {
                          //llvm::errs() << "Skipping non struct arg type \n";
                          continue;
                      }
                      if (id1->getType() == primitiveFuncArgT && (isFunctionPointer(type) ||
                                     type->isPointerTy() || type->isStructTy()))  {
                        //llvm::errs() << "Skipping non primitive arg type \n";
                        continue;
                      }
                      //llvm::errs() << "value " << (*pv) << "\n";
                      std::tuple<NodeID, const Function*, const Type *> t = std::make_tuple(sn->getId(), pn->getFunction(), pv->getType());
                      //llvm::errs() << "Checking unique res\n";
                      if (uniqRes.find(t) == uniqRes.end())
                         uniqRes.insert(t);
                      else continue;
                      //llvm::errs() << "inserting actual par: " << sn->getId() << "\n";
                      if (id1->getArgNo() < 0 || matchArgNo(svfg->getSVFGNode(sn->getId()), id1->getArgNo())) {
                         FunctionArgEntity *fae = new FunctionArgEntity(svfg->getSVFGNode(sn->getId()),
                                                          pn->getFunction(), pv->getType());
                         results.insert(std::make_pair(fae, ent));
                      }
                    }
                 }
              }
           }
        }
      }
      NodeBS &ains = svfg->getActualINSVFGNodes(cs);
      for(auto ain : ains) {
          ActualINSVFGNode *svfgnode = (ActualINSVFGNode*)svfg->getSVFGNode(ain);
          llvm::errs() << "actual in: " << svfgnode->getId() << "\n";
          const PointsTo &pts = svfgnode->getPointsTo();
          const PAGNode* node = NULL;
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
              node = pag->getPAGNode(ptd);
              //llvm::errs() << "PAG node: " << pag << "\n";
              if (!node) continue;
              if ((node->getNodeKind() != PAGNode::DummyValNode) && (node->getNodeKind() != PAGNode::DummyObjNode)) {
                 Type *type = node->getValue()->getType();
                 //llvm::errs() << "id name=" << id1->getName() << " type=" << id1->getType() << " type=" << getTypeName(type) << "\n";
                 if (id1->getType() == funcptrFuncArgT && !isFunctionPointer(type)) {
                    //llvm::errs() << "Skipping non func pointer arg type \n";
                    continue;
                 }
                 if (id1->getType() == pointerFuncArgT && !type->isPointerTy()) {
                   //llvm::errs() << "Skipping non pointer arg type \n";
                   continue;
                 }
                 if (id1->getType() == structFuncArgT && !type->isStructTy()) {
                   //llvm::errs() << "Skipping non struct arg type \n";
                   continue;
                 }
                 if (id1->getType() == primitiveFuncArgT && (isFunctionPointer(type) ||
                                     type->isPointerTy() || type->isStructTy()))  {
                   //llvm::errs() << "Skipping non primitive arg type \n";
                   continue;
                 }
                 std::tuple<NodeID, const Function*, const Type *> t = 
                             std::make_tuple(svfgnode->getId(), node->getFunction(), node->getValue()->getType());
                 //llvm::errs() << "Checking unique res\n";
                 if (uniqRes.find(t) == uniqRes.end())
                    uniqRes.insert(t);
                 else continue;
                 //llvm::errs() << "inserting actual in: " << svfgnode->getId() << "\n";
                 if (id1->getArgNo() < 0 || matchArgNo(svfgnode, id1->getArgNo())) {
                    FunctionArgEntity *fae = new FunctionArgEntity(svfgnode, node->getFunction(), node->getValue()->getType());
                    results.insert(std::make_pair(fae, ent));
                 }
              }
          }
      }
  

    if (InfoFlowIncludeFormals && cs.getCalledValue()) {
      if (Function *func = SVFUtil::dyn_cast<Function>(cs.getCalledValue()->stripPointerCasts())) { 
      NodeBS &ains = svfg->getFormalINSVFGNodes(func);
      for(auto ain : ains) {
          ActualINSVFGNode *svfgnode = (ActualINSVFGNode*)svfg->getSVFGNode(ain);
          llvm::errs() << "actual in: " << svfgnode->getId() << "\n";
          const PointsTo &pts = svfgnode->getPointsTo();
          const PAGNode* node = NULL;
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
              node = pag->getPAGNode(ptd);
              //llvm::errs() << "PAG node: " << pag << "\n";
              if (!node) continue;
              if ((node->getNodeKind() != PAGNode::DummyValNode) && (node->getNodeKind() != PAGNode::DummyObjNode)) {
                 Type *type = node->getValue()->getType();
                 //llvm::errs() << "id name=" << id1->getName() << " type=" << id1->getType() << " type=" << getTypeName(type) << "\n";
                 if (id1->getType() == funcptrFuncArgT && !isFunctionPointer(type)) {
                    //llvm::errs() << "Skipping non func pointer arg type \n";
                    continue;
                 }
                 if (id1->getType() == pointerFuncArgT && !type->isPointerTy()) {
                   //llvm::errs() << "Skipping non pointer arg type \n";
                   continue;
                 }
                 if (id1->getType() == structFuncArgT && !type->isStructTy()) {
                   //llvm::errs() << "Skipping non struct arg type \n";
                   continue;
                 }
                 if (id1->getType() == primitiveFuncArgT && (isFunctionPointer(type) ||
                                     type->isPointerTy() || type->isStructTy()))  {
                   //llvm::errs() << "Skipping non primitive arg type \n";
                   continue;
                 }
                 std::tuple<NodeID, const Function*, const Type *> t = 
                             std::make_tuple(svfgnode->getId(), node->getFunction(), node->getValue()->getType());
                 //llvm::errs() << "Checking unique res\n";
                 if (uniqRes.find(t) == uniqRes.end())
                    uniqRes.insert(t);
                 else continue;
                 //llvm::errs() << "inserting actual in: " << svfgnode->getId() << "\n";
                 if (id1->getArgNo() < 0 || matchArgNo(svfgnode, id1->getArgNo())) {
                    FunctionArgEntity *fae = new FunctionArgEntity(svfgnode, node->getFunction(), node->getValue()->getType());
                    results.insert(std::make_pair(fae, ent));
                 }
              }
          }        
      }
     }
   }
  }
  return new BinarySolution(label, results);  
}


BinarySolution *InfoFlowEngine::solveActualParameterPasses(Label *label, Identifier *id1, Solution *sol1, Identifier *id2,
                                                          Solution *sol2) {

   assert(sol2->isUnary() && "Expected a unary solution for arg in optimized path!\n");
   //llvm::errs() << "Optimizing funarg query!\n";
   PAG *pag = svfg->getPAG();
   QueryExp *q = sol2->getLabel()->getQueryExp();
   std::set<std::pair<Entity*, Entity*> > results;
   std::set<Entity*> res = sol1->getResults();
   for(auto e : res) {
      CallSite cs = *e->getCallsite();
      if (pag->getCallSiteArgsMap().find(cs) != pag->getCallSiteArgsMap().end()) {
         std::list<const PAGNode*>& arglist = pag->getCallSiteArgsMap()[cs];
         for(std::list<const PAGNode*>::iterator lit = arglist.begin(); lit != arglist.end(); lit++) {
           if (svfg->PAGNodeToActualParmMap.find(std::make_pair((*lit)->getId(),cs)) != 
                   svfg->PAGNodeToActualParmMap.end()) {
              const SVFGNode* sn = svfg->getActualParmVFGNode(*lit,cs);
              if (!sn) continue;
              if (const ActualParmSVFGNode* ap = SVFUtil::dyn_cast<ActualParmSVFGNode>(sn)) {
                 const PAGNode *pn = ap->getParam();
                 if (!pn) continue;
                 if ((pn->getNodeKind() != PAGNode::DummyValNode) && (pn->getNodeKind() != PAGNode::DummyObjNode)) {
                    const Value *pv = pn->getValue();
                    if (pv) {
                      Type *type = pv->getType();
                      if (id2->getType() == funcptrFuncArgT && !isFunctionPointer(type)) {
                         //llvm::errs() << "Skipping non func pointer arg type \n";
                         continue;
                      }
                      if (id2->getType() == pointerFuncArgT && !type->isPointerTy()) {
                         //llvm::errs() << "Skipping non pointer arg type \n";
                         continue;
                      }
                      if (id2->getType() == structFuncArgT && !type->isStructTy()) {
                          //llvm::errs() << "Skipping non struct arg type \n";
                          continue;
                      }
                      if (id2->getType() == primitiveFuncArgT && (isFunctionPointer(type) ||
                                     type->isPointerTy() || type->isStructTy()))  {
                        //llvm::errs() << "Skipping non primitive arg type \n";
                        continue;
                      }
                      //llvm::errs() << "value " << (*pv) << "\n";
                      FunctionArgEntity *fae = new FunctionArgEntity(svfg->getSVFGNode(sn->getId()), 
                                                          pn->getFunction(), pv->getType());
                      results.insert(std::make_pair(fae, e));
                    }
                 }
              }
           }
        } 
      }
       
      NodeBS &ains = svfg->getActualINSVFGNodes(cs);
      for(auto ain : ains) {
          ActualINSVFGNode *svfgnode = (ActualINSVFGNode*)svfg->getSVFGNode(ain);
          //llvm::errs() << "actual in: " << svfgnode->getId() << "\n";
          const PointsTo &pts = svfgnode->getPointsTo();
          const PAGNode* node = NULL;
          for(PointsTo::iterator ii = pts.begin(), ie = pts.end();
                ii != ie; ii++) {
              NodeID ptd = *ii;
              node = pag->getPAGNode(ptd);
              //llvm::errs() << "PAG node: " << pag << "\n";
              if (!node) continue;
              if ((node->getNodeKind() != PAGNode::DummyValNode) && (node->getNodeKind() != PAGNode::DummyObjNode)) {
                 Type *type = node->getValue()->getType();
                 if (id2->getType() == funcptrFuncArgT && !isFunctionPointer(type)) {
                    //llvm::errs() << "Skipping non func pointer arg type \n";
                    continue;
                 }
                 if (id2->getType() == pointerFuncArgT && !type->isPointerTy()) {
                   //llvm::errs() << "Skipping non pointer arg type \n";
                   continue;
                 }
                 if (id2->getType() == structFuncArgT && !type->isStructTy()) {
                   //llvm::errs() << "Skipping non struct arg type \n";
                   continue;           
                 }
                 if (id2->getType() == primitiveFuncArgT && (isFunctionPointer(type) || 
                                     type->isPointerTy() || type->isStructTy()))  {
                   //llvm::errs() << "Skipping non primitive arg type \n";
                   continue;
                 }
                 FunctionArgEntity *fae = new FunctionArgEntity(svfgnode, node->getFunction(), node->getValue()->getType());
                 results.insert(std::make_pair(fae, e));
              }
          }
      }
   }
   return new BinarySolution(label, results);
}


Solution *InfoFlowEngine::solveForActualparameterpassedby(Label *label, Identifier *id1, Solution *sol1, Identifier *id2, 
                                                          Solution *sol2) {
    /*
    std::map<std::string, std::set<Entity*>*> cl1 = Entity::generateClusters(sol1->getResults());
    stats << "Number of clusters for " << id1->getName() << "=" << cl1.size() << "\n";
    llvm::errs() << "Number of clusters for " << id1->getName() << "=" << cl1.size() << "\n";
    for(auto clp : cl1)  {
        llvm::errs() << "cluster " << clp.first << " size=" << clp.second << "\n";
        stats << "cluster " << clp.first << " size=" << clp.second << "\n";
    }
    std::map<std::string, std::set<Entity*>*> cl2 = Entity::generateClusters(sol2->getResults());
    stats << "Number of clusters for " << id2->getName() << "=" << cl2.size() << "\n";
    llvm::errs() << "Number of clusters for " << id2->getName() << "=" << cl2.size() << "\n";
    for(auto clp : cl2) {
        llvm::errs() << "cluster " << clp.first << " size=" << clp.second << "\n";
        stats << "cluster " << clp.first << " size=" << clp.second << "\n";
    }*/
    BinarySolution *res = solveActualParameterPassedby(label, id1, sol1, id2, sol2);
    /*if (sol1->isUnary()) // optimizing solution
        res = solveActualParameterPasses(label, id2, sol2, id1, sol1);
    else 
        res = sol1->filterActualparameterpassedby(label, sol2);*/


    return res;
}

Solution *InfoFlowEngine::solveForIndirectCallOf(Label *label, Identifier *id1, Solution *sol1, Identifier *id2, Solution *sol2) {
    BinarySolution *res = sol1->filterIndirectCallOf(label, sol2);
    return res;
}

void recordReaches(NodeID n1, std::string cst, NodeID n2) {
   if (reachableBy.find(n2) == reachableBy.end())
      reachableBy[n2] = new std::set<std::pair<std::string,NodeID> >();
    reachableBy[n2]->insert(std::make_pair(cst,n1));
}

void recordDoesnotreach(NodeID n1, std::string cst, NodeID n2) {
   if (unreachableBy.find(n2) == unreachableBy.end())
      unreachableBy[n2] = new std::set<std::pair<std::string,NodeID> >();
   unreachableBy[n2]->insert(std::make_pair(cst,n2));
}

bool cannotReach(NodeID n1, std::string cst, NodeID n2) {
   if (unreachableBy.find(n2) == unreachableBy.end()) 
      return false;
   for(auto n : (*unreachableBy[n2])) {
      if (n.second == n1 && n.first == cst) return true;
      if (reachableBy.find(n.second) != reachableBy.end()) {
         for(auto n3 : (*reachableBy[n.second]))
            if (n3.second == n1 && n3.first == cst) 
               return true; 
      }
  }
  return false;
}

bool canReach(NodeID n1, std::string cst, NodeID n2) {
   if (reachableBy.find(n2) == reachableBy.end())
      return false;
   for(auto n : (*reachableBy[n2])) {
      if (n.second == n1 && n.first == cst) return true;
      if (reachableBy.find(n1) != reachableBy.end()) {
         for(auto n3 : (*reachableBy[n1]))
            if (n3.second == n1 && n3.first == cst)
            return true;
      }
  }
  return false;
}

void InfoFlowEngine::printResults(Label *label, std::string desc, std::set<std::pair<Entity*,Entity*> > &r) {
  if (InfoFlowVerbose) {
      stats << "Label " << label->getDescription() << " results:\n";
      stats << "\n================================\n";
  }
  llvm::errs() << "Label " << label->getDescription() << " results:\n";
  for(auto re : r) {
     llvm::errs() << re.first->getStringRep() << " [" << desc << "] " << re.second->getStringRep() << "\n";   
     if (InfoFlowVerbose) {
        if (re.first->getNode() && re.second->getNode()) 
           stats << "Node " << re.first->getNode()->getId()  <<  " [" << desc << "] Node " << re.second->getNode()->getId() << "\n";
        stats << re.first->getStringRep() <<  " [" << desc << "] " << re.second->getStringRep() << "\n";   
    }
  }
  if (InfoFlowVerbose)
      stats << "\n================================\n";
}

void InfoFlowEngine::printPath(std::vector<const BasicBlock*> &path) {
   if (InfoFlowVerbose)
      stats << "\n================INTRAPROC PATH================\n";
   for(int i=0; i<path.size(); i++) {
      llvm::errs() << getInstructionStr(path[i]->getTerminator()) + " " +
              SVFUtil::getSourceLoc(path[i]->getTerminator()) + "\n";
      if (InfoFlowVerbose)
         stats << getInstructionStr(path[i]->getTerminator()) + " " +
              SVFUtil::getSourceLoc(path[i]->getTerminator()) << " " << ",";
   }
   if (InfoFlowVerbose)
      stats << "\n================================\n";
}

void InfoFlowEngine::printPath(std::vector<NodeID> &path) {
   if (InfoFlowVerbose) 
      stats << "\n================SVFG PATH================\n";
   for(int i=0; i<path.size(); i++) {
      SVFGNode *spn = svfg->getSVFGNode(path[i]);
      llvm::errs() << "(" << path[i] << ")" << prettyPrint(spn) << ", ";
      if (InfoFlowVerbose) 
         stats << prettyPrint(spn) << " " << ",";
   }
   if (InfoFlowVerbose) 
      stats << "\n================================\n";
}


void InfoFlowEngine::printICFGPath(std::vector<NodeID> &path, SVFGNode *node1, SVFGNode *node2) {
   
   if (InfoFlowVerbose) {
      stats << "Source svfgnode=" << node1->getId() << " " << getNodeInstruction(node1) << " " << prettyPrint(node1) 
           << "\nDest svfgnode=" << node2->getId() << " " << getNodeInstruction(node2) << " " << prettyPrint(node2) << "\n"; 
      stats << "\n================ICFG PATH================\n";
   }
   for(int i=0; i<path.size(); i++) {
      ICFGNode *in = icfg->getICFGNode(path[i]);
      llvm::errs() << "(" << path[i] << ")" << prettyPrint(in) << ", ";
      if (InfoFlowVerbose)
         stats << "(" << path[i] << ")" << prettyPrint(in) << " " << ",";
   }
   if (InfoFlowVerbose)
      stats << "\n================================\n";
}


bool isReachableICFG(const ICFGNode *sn, const ICFGNode *en, 
                     std::set<const ICFGNode*> &visited, std::set<const ICFGNode*> &sanbb,
                     std::vector<NodeID> &rp, std::vector<CallSiteID> &context, int depth) {
  if (sn == en) {
     if (sanbb.find(sn) != sanbb.end()) {
         //llvm::errs() << "Reachablity check at ICFG fails due to sanitization node " << sn->getId() << "\n";
         return false;
     }
     else return true;
  }
  if (visited.find(sn) != visited.end()) {
     //llvm::errs() << "bb visited before " << (*ebb->getTerminator()) << "\n";
     return false;
  }
  if (InfoFlowBound != 0 && depth > InfoFlowBound) {
      //llvm::errs() << "WARNING: infoflow bound " << InfoFlowBound << " reached!\n";
      return false;
  }
  //llvm::errs() << "visiting node " << (*ebb->getTerminator()) << "\n";
  visited.insert(sn);
  if (sanbb.find(sn) != sanbb.end()) {
     //llvm::errs() << "Reachablity check at ICFG fails due to sanitization node " << sn->getId() << "\n";
     return false;
  }
  rp.push_back(sn->getId());
  for(GenericNode<ICFGNode,ICFGEdge>::GEdgeSetTy::const_iterator it = sn->getOutEdges().begin(), eit = sn->getOutEdges().end(); 
                                                                     it!=eit; ++it){
    


     if (const CallCFGEdge *ce = SVFUtil::dyn_cast<CallCFGEdge>(*it)) {
        context.push_back(ce->getCallSiteId());
     }  
     else if (const RetCFGEdge *re = SVFUtil::dyn_cast<RetCFGEdge>(*it)) {
        if (context.size() > 0) {
           if (context.back() != re->getCallSiteId())
              return false;
           context.pop_back();
        }
     }

      ICFGNode* succ = (*it)->getDstNode();
      //llvm::errs() << "pred: " << (*pred->getTerminator()) << "\n";
      if (sanbb.size() > 0) {
         std::set<const ICFGNode*> visitedp;
         visitedp = visited;
         if (isReachableICFG(succ, en, visitedp, sanbb, rp, context, depth+1)) {
            //llvm::errs() << "a reachable cf path!\n";
            return true;
         }
      }
      else if (isReachableICFG(succ, en, visited, sanbb, rp, context, depth+1)) {
         return true;
      }
  }
  rp.pop_back();
  //llvm::errs() << "backtracking from node " << en->getId() << " " << prettyPrint(en) << "\n";
  return false;
}

bool isReachableICFG(const ICFGNode *sn, const ICFGNode *en,
                     std::set<const ICFGNode*> &visited, std::set<const ICFGNode*> &sanbb,
                     std::vector<NodeID> &rp, std::vector<CallSiteID> &context) {
   return isReachableICFG(sn, en, visited, sanbb, rp, context, 0);
}

bool isReachableBB(const BasicBlock *sbb, const BasicBlock *ebb, 
                   std::set<const BasicBlock*> &visited, std::set<const BasicBlock*> &sanbb, 
                   std::vector<const BasicBlock*> &path) {
  llvm::errs() << "trying node " << (*ebb->getTerminator()) << "\n";
  if (visited.find(ebb) != visited.end()) {
     //llvm::errs() << "bb visited before " << (*ebb->getTerminator()) << "\n";
     return false;
  }
  //llvm::errs() << "visiting node " << (*ebb->getTerminator()) << "\n";
  visited.insert(ebb);
  path.insert(path.begin(), ebb);
  if (sanbb.find(ebb) != sanbb.end()) {
     //llvm::errs() << "Rejecting due to sanitization node " << (*ebb->getTerminator());
     return false;
  }
  for (auto it = pred_begin(ebb), et = pred_end(ebb); it != et; ++it) {
      const BasicBlock* pred = *it;
      if (sbb == pred) {
         if (sanbb.find(sbb) == sanbb.end()) 
            return true;
      }

      //llvm::errs() << "pred: " << (*pred->getTerminator()) << "\n";
      std::set<const BasicBlock*> visitedp;
      visitedp = visited;
      if (isReachableBB(sbb, pred, visitedp, sanbb, path)) {
         //llvm::errs() << "a reachable cf path!\n";
         return true;
      }
  }
  path.erase(path.begin());
  //llvm::errs() << "backtracking from node " << (*ebb->getTerminator()) << "\n";
  return false;
}

Constant *getConstantOperand(const Instruction *inst, int &val) {
 if (llvm::Constant *c = llvm::dyn_cast<Constant>(inst->getOperand(0))) {
    val = c->getUniqueInteger().getLimitedValue();
    return c;
 }
 else if (llvm::Constant *c = llvm::dyn_cast<Constant>(inst->getOperand(1))) {
    val = c->getUniqueInteger().getLimitedValue();
    return c;
 }
 else return NULL;  
}

void followAndCompute(const Instruction *inst, const BranchInst *target, int &pos, bool &precise,
                             std::set<BasicBlock*> &Ttargets,
                             std::set<BasicBlock*> &Ftargets, 
                             std::set<const Instruction*> &visited, int retval) {
  if (visited.find(inst) != visited.end())
     return;
  visited.insert(inst);
  if (const llvm::ICmpInst *cmp = llvm::dyn_cast<llvm::ICmpInst>(inst)) {
     if (cmp->isEquality()) {
         llvm::ICmpInst::Predicate p = cmp->getPredicate();
         int val;
         if (!getConstantOperand(cmp, val)) 
            return; 
         //if (val != 0 && val != 1) 
         //   return;
         //llvm::errs() << "constant op of cmp=" << val << "\n";
         if (p == llvm::ICmpInst::ICMP_EQ) {
            llvm::errs() << "Comparing in EQ " << val << " vs " << retval << "\n";
            if (val == retval)
               pos = 1;
            else pos = 0;
            /*if (val == 0 && retval == 0)
               pos = true;
            if (val == 0 && retval == 1)
               pos = false;
            if (val == 1 && retval == 0)
               pos = false;
            if (val == 1 && retval == 1)
               pos = true;*/
            //llvm::errs() << "truth value after EQ =" << pos << "\n";   
         }
         else if (p == llvm::ICmpInst::ICMP_NE) {
            if (val != retval)
               pos = 1;
            else { 
               pos = 0;
               precise = true;
            }
            /*if (val == 0 && retval == 0)
               pos = false;
            if (val == 0 && retval == 1)
               pos = true;
            if (val == 1 && retval == 0)
               pos = true;
            if (val == 1 && retval == 1)
               pos = false;
            */
            //llvm::errs() << "truth value after NEQ =" << pos << "\n";   
         }
         else return;
     }
     else return;
  }
  else if (inst == target) {
     Ttargets.insert(target->getSuccessor(0));
     Ftargets.insert(target->getSuccessor(1));     
     return;
     /*if (pos) {
           llvm::errs() << "target inst: " << (*target) << "\n";
           llvm::errs() << "adding " << (*target->getSuccessor(0)->getTerminator()) << " to true case "
                        << " and " << (*target->getSuccessor(1)->getTerminator()) << " to false case\n"; 
           Ttargets.insert(target->getSuccessor(0));
           Ftargets.insert(target->getSuccessor(1));
     }
     else {
           llvm::errs() << "target inst: " << (*target) << "\n";
           llvm::errs() << "adding " << (*target->getSuccessor(1)->getTerminator()) << " to true case "
                        << " and " << (*target->getSuccessor(0)->getTerminator()) << " to false case\n"; 
           Ttargets.insert(target->getSuccessor(1));
           Ftargets.insert(target->getSuccessor(0));
     } */      
  }

  if (inst->getOpcode() == Instruction::Xor) {
     int val;
     if (getConstantOperand(inst, val)) {
        if (!pos && val == 0)    
           pos = 1;
        else if (pos && val == 1)
           pos = 0;     
     }
  }

  if (inst->getOpcode() == Instruction::Store) { // check if it is a store load pattern
     llvm::errs() << "Found store that may lead to branch " << (*inst) << "\n"; 
     BasicBlock *bb = (BasicBlock*)inst->getParent();
     bool seen = false;
     Instruction *l = NULL;
     for(BasicBlock::iterator it = bb->begin(); it != bb->end(); it++) {
        if (&(*it) == inst) { 
           seen = true;  
           continue;   
        }  
        if (seen) {
           if ((*it).getOpcode() == Instruction::Load && (*it).getOperand(0) == inst->getOperand(1)) {
              l = &(*it);
              break;
           }
       }
     }
     if (l) {
        llvm::errs() << "Found load that may lead to branch " << (*l) << "\n"; 
        inst = l;
     }
     else {
        //assert(0 && "Unhandled pattern in follow and compute\n");
        return;
     }
  }


   BasicBlock *bb = (BasicBlock*)inst->getParent();
   for(BasicBlock::iterator it = bb->begin(); it != bb->end(); it++) {
      if (&(*it) == inst) continue;
     for(Use &U : (*it).operands())  {
      if (Instruction *ii = llvm::dyn_cast<Instruction>(U)) {
         if (ii == inst) {
            //llvm::errs() << "Following use " << (*it) << "\n";
            followAndCompute(&(*it), target, pos, precise, Ttargets, Ftargets, visited, retval);
         }
      }
    }
  }
}

ICFGNode *findClosestSuccWithConditionalBB(const BasicBlock *bb, ICFGNode *n, std::string &fname) {
  const BasicBlock *nbb = n->getBB();
  if (const BranchInst *term = llvm::dyn_cast<BranchInst>(nbb->getTerminator())) 
     if (!term->isUnconditional())
        return n;  
  for(GenericNode<ICFGNode,ICFGEdge>::GEdgeSetTy::const_iterator it = n->getOutEdges().begin(),
                                           eit = n->getOutEdges().end(); it!=eit; ++it){
        ICFGNode* succ = (*it)->getDstNode();
        if (SVFUtil::isa<FunExitBlockNode>(succ))
           fname = succ->getBB()->getParent()->getName();
        return findClosestSuccWithConditionalBB(bb, succ, fname);   
  }
  return NULL;
}


const Instruction *InfoFlowEngine::followInstrRetValue(const Instruction *inst, const BasicBlock *bb) {
  if (const BranchInst *term = llvm::dyn_cast<BranchInst>(bb->getTerminator())) {
     if (term->isUnconditional()) return NULL;
     return inst;
  }
  else {// must be a return
     ICFGNode *in = getICFGNode(bb);
     std::string ifname = bb->getParent()->getName();
     in = findClosestSuccWithConditionalBB(bb, in, ifname);
     if (!in) return NULL;
     llvm::errs() << "Closest succ node with a different bb " << in->getId()
                  << " br inst: " << (*in->getBB()->getTerminator())
                  << " holding a callsite for " << ifname << "\n";
     bool csfound = false;
     const Instruction *cinst = NULL;
     for(BasicBlock::const_iterator it = in->getBB()->begin(); it != in->getBB()->end(); it++) {
        const Instruction *ci = &(*it);
        llvm::errs() << "candidate callsite: " << (*ci) << "\n";
        if (ci->getOpcode() == Instruction::Call || ci->getOpcode() == Instruction::Invoke) {
           CallSite cs = SVFUtil::getLLVMCallSite(ci);
           if (Function *callee = SVFUtil::dyn_cast<Function>(cs.getCalledValue()->stripPointerCasts())) {
              llvm::errs() << "callee name=" << callee->getName() << " vs " << ifname << "\n";
              if (callee->getName() == ifname)  {
                 cinst = ci;
                 csfound = true;
                 break;
              }
           }
        }
     }
     if (csfound) 
        return cinst;
     else return NULL;
  }
  return NULL;
}

void InfoFlowEngine::findCases(const Instruction *inst, const BasicBlock *bb, std::set<BasicBlock*> &trueTargets,
                                std::set<BasicBlock*> &falseTargets, int retval, int &truth, bool &precise) {
  if (const BranchInst *term = llvm::dyn_cast<BranchInst>(bb->getTerminator())) {
     if (term->isUnconditional()) return;
     std::set<const Instruction*> visited;
     followAndCompute(inst, term, truth, precise, trueTargets, falseTargets, visited, retval);  
  }
  else {// must be a return
     ICFGNode *in = getICFGNode(bb);
     std::string ifname = bb->getParent()->getName();
     in = findClosestSuccWithConditionalBB(bb, in, ifname);
     if (!in) return;
     llvm::errs() << "Closest succ node with a different bb " << in->getId() 
                  << " br inst: " << (*in->getBB()->getTerminator()) 
                  << " holding a callsite for " << ifname << "\n";
     bool csfound = false;
     const Instruction *cinst = NULL;
     for(BasicBlock::const_iterator it = in->getBB()->begin(); it != in->getBB()->end(); it++) {
        const Instruction *ci = &(*it);
        llvm::errs() << "candidate callsite: " << (*ci) << "\n";
        if (ci->getOpcode() == Instruction::Call || ci->getOpcode() == Instruction::Invoke) {
           CallSite cs = SVFUtil::getLLVMCallSite(ci);
           if (Function *callee = SVFUtil::dyn_cast<Function>(cs.getCalledValue()->stripPointerCasts())) {
              llvm::errs() << "callee name=" << callee->getName() << " vs " << ifname << "\n";
              if (callee->getName() == ifname)  {
                 cinst = ci;
                 csfound = true;
                 break;
              }
           }
        }
     } 
     if (csfound) {
        std::set<const Instruction*> visited;
        followAndCompute(cinst, (BranchInst*)in->getBB()->getTerminator(), truth, precise, trueTargets, falseTargets, visited, retval);        
     }
  }
}

bool isCtrlFlowSan(const Instruction *inst) {
  if (!inst) return false;
  const BasicBlock *bb = inst->getParent();
  const BranchInst *bi = llvm::dyn_cast<BranchInst>(bb->getTerminator());
  if (bi->isUnconditional())
     return false;
  return usesOf(inst, bi, bb);
}

const Value *findStoreUse(const Instruction *inst) {
  const BasicBlock *bb = inst->getParent();
  for(BasicBlock::const_iterator it = bb->begin(); it != bb->end(); it++) {
     llvm::errs() << "Scanning inst " << (*it) << " " << (*inst) << "\n";
     if (const StoreInst *st = llvm::dyn_cast<StoreInst>(&(*it))) {
        llvm::errs() << "Is store " << (*st) << " user of " << (*inst) << "\n";
        if (st->getOperand(0) == inst)
           return st->getOperand(1);
     }
  }
  return NULL;
}

const Instruction *useInBB(const Value *val, const BasicBlock *bb) {
  if (!val) return NULL;
  for(BasicBlock::const_iterator it = bb->begin(); it != bb->end(); it++) { 
     llvm::errs() << "Is a user " << prettyPrint(&(*it)) << " of Sanitizer? " << (*val) << "\n";
     const Instruction *ii = &(*it);
     for(const llvm::Use &U : ii->operands()) {
        const Use *u = &U;
        if (U.get() == val) { 
           llvm::errs() << "User of Sanitizer: " << (*u->getUser()) << "\n";
           if (Instruction *ui = llvm::dyn_cast<Instruction>(u->getUser())) {
              if (ui->getParent() == bb)
                 return ii;
           }
        }
     }
  }
  return NULL;
}

void InfoFlowEngine::exploreUsesInSuccessors(const Instruction *inst, const Instruction *orig, const Value *storeUse, const BasicBlock *fb, 
                             std::set<const BasicBlock*> &sanbb, std::set<std::string> &fnames, int retval, 
                             std::set<const BasicBlock*> &visited) {
   if (visited.find(fb) != visited.end())
      return;
   visited.insert(fb);
   const Instruction *diruse = useInBB(orig, fb);
   const Instruction *suse = useInBB(storeUse, fb);
   if (!diruse && !suse) {
      llvm::errs() << "Sanitization exploring successor basic blocks for " << prettyPrint(fb->getTerminator()) << "\n";
      if (const BranchInst *bi = llvm::dyn_cast<BranchInst>(fb->getTerminator())) {
          if (bi->isUnconditional())
             exploreUsesInSuccessors(inst, orig, storeUse, (BasicBlock*)bi->getOperand(0), sanbb, fnames, retval, visited);
          else {
             exploreUsesInSuccessors(inst, orig, storeUse, (BasicBlock*)bi->getOperand(1), sanbb, fnames, retval, visited);
             exploreUsesInSuccessors(inst, orig, storeUse, (BasicBlock*)bi->getOperand(2), sanbb, fnames, retval, visited);
          }
      }
   }
   else if (diruse) {
      llvm::errs() << "Direct use of Sanitizer " << (*inst) << " is " << (*diruse) << "\n"; 
      filterSanitizationBB(diruse, orig, sanbb, fnames, retval);
   }
   else if (suse) {
      llvm::errs() << "Indirect use of Sanitizer " << (*inst) << " is " << (*suse) << "\n"; 
      filterSanitizationBB(suse, orig, sanbb, fnames, retval);
   }
}



void InfoFlowEngine::filterSanitizationBB(std::set<NodeID> &sanitizers, std::set<const BasicBlock*> &sanbb, 
                                          std::set<std::string> &fnames, std::vector<int> retvalV) {
 for(int si=0; si < retvalV.size(); si++) {
  int retval = retvalV[si];
  llvm::errs() << "Sanitization value=" << retval << "\n";
  for(auto san : sanitizers) {
     SVFGNode *node = svfg->getSVFGNode(san);
     bool cfrelevant = false;
     if (const Instruction *inst = getNodeInstruction(node)) {
         if (inst->getOpcode() == Instruction::Call || inst->getOpcode() == Instruction::Invoke)  {
            CallSite cs = SVFUtil::getLLVMCallSite(inst);
            if (const Function *func = cs.getCalledFunction()) {
               if (func->getReturnType() && !func->getReturnType()->isVoidTy()) {
                  cfrelevant = true;
               }
            }
         }
         if (cfrelevant && usesOf(inst, inst->getParent()->getTerminator(), inst->getParent())) {
            const Instruction *rinst = followInstrRetValue(inst, inst->getParent());
            if (rinst) {
               filterSanitizationBB(rinst, rinst, sanbb, fnames, retval);  
            }
            else {
               const BasicBlock *bb = inst->getParent();
               sanbb.insert(bb);
               fnames.insert(((Function*)bb->getParent())->getName());
            }                       
         }
         else {
            const BasicBlock *bb = inst->getParent();
            sanbb.insert(bb);
            fnames.insert(((Function*)bb->getParent())->getName());
         }
     }
  }
 }
}

void InfoFlowEngine::filterSanitizationBB(const Instruction *inst, const Instruction *orig, std::set<const BasicBlock*> &sanbb, 
                                          std::set<std::string> &fnames, int retval) {

            llvm::errs() << "Filtering Sanitization for " << (*inst) << "\n";
            const Value *storeUse = findStoreUse(orig);
            // Find the successor that depends on the true evaluation of the sanitizer
            std::set<BasicBlock*> tcases, fcases;
            llvm::errs() << "Finding cases for " << (*inst) << "\n";
            int truth = -1;
            bool precise = false;
            findCases(inst, inst->getParent(), tcases, fcases, retval, truth, precise);
            //llvm::errs() << "true cases:\n";
            //for(auto tbb : tcases) {
            //   llvm::errs() << "\t" << (*tbb->getTerminator()) << "\n";
            //}
            //llvm::errs() << "false cases:\n";
            //for(auto fbb : fcases) {
            //   llvm::errs() << "\t" << (*fbb->getTerminator()) << "\n";
            //}

            if (truth == 0) {
               if (precise) {
                  llvm::errs() << "Ret value " << retval << " chooses false cases\n";
                  if (tcases.size() <= 1 && fcases.size() <= 1) {
                     for(auto fb : fcases) {
                        if (tcases.find(fb) == tcases.end()) {
                           llvm::errs() << "Sanitizer terminated by false branch " << prettyPrint(fb->getTerminator()) << "\n";
                           sanbb.insert(fb);
                           fnames.insert(((Function*)fb->getParent())->getName());
                        }
                     }
                  }                  
               }
               else {
                  if (tcases.size() <= 1 && fcases.size() <= 1) {
                     for(auto fb : fcases) {
                        if (tcases.find(fb) == tcases.end()) {
                           llvm::errs() << "Sanitizer terminated by false branch " << prettyPrint(fb->getTerminator()) << "\n";
                           std::set<const BasicBlock*> visited; 
                           exploreUsesInSuccessors(inst, orig, storeUse, fb, sanbb, fnames, retval, visited);
                           //sanbb.insert(fb);
                           //fnames.insert(((Function*)fb->getParent())->getName());
                        }
                     }
                  } 
              }
            }
            else if (truth == 1) {
               llvm::errs() << "Ret value " << retval << " chooses true cases\n";
               if (tcases.size() <= 1 && fcases.size() <= 1) {
                  for(auto tb : tcases) {
                     if (fcases.find(tb) == fcases.end()) {
                        llvm::errs() << "Sanitizer terminated by true branch " << prettyPrint(tb->getTerminator()) << "\n";
                        sanbb.insert(tb);
                        fnames.insert(((Function*)tb->getParent())->getName());
                     }
                  }
               } 
            }
}

void InfoFlowEngine::dividePerFunc(std::vector<NodeID> &path, std::set<std::string> &pathfnames,
                   std::vector<std::vector<const BasicBlock*> *> &bbpaths) {
  std::string cfname = "";
  std::vector<const BasicBlock*> *bv = new std::vector<const BasicBlock*>();
  for(int i=0; i<path.size(); i++) {
     SVFGNode *node = svfg->getSVFGNode(path[i]);
     if (const Instruction *inst = getNodeInstruction(node)) {
         const BasicBlock *bb = inst->getParent();
         /*if (sanbb.find(bb) != sanbb.end()) {
            llvm::errs() << "Path terminating due to bb " << (*bb->getTerminator()) << "\n";
            return false;
         }*/
         std::string fname = ((Function*)bb->getParent())->getName();
         pathfnames.insert(fname);
         if (cfname == "") {
            bbpaths.push_back(bv);
         }
         else if (cfname != fname) {
            bv = new std::vector<const BasicBlock*>();
            bbpaths.push_back(bv);
         }
         cfname = fname;          
         if (bv->size() > 0) {
            if (bb != bv->back())
               bv->push_back(bb);
         }
         else bv->push_back(bb);
     }
  }
}

bool InfoFlowEngine::validCFL(std::vector<NodeID> &path,
                              std::set<const BasicBlock*> &sanbb,
                              std::set<std::string> &fnames) {
  if (sanbb.size() == 0) return true;
  if (path.size() == 0) return false;
  std::vector<std::vector<const BasicBlock*>*> bbperfunc;
  std::set<std::string> pathfnames;
  dividePerFunc(path, pathfnames, bbperfunc);
  if (bbperfunc.size() == 0)
     return false;
  //llvm::errs() << "Did path segment further divided into subsegments per function? " 
  //             << (bbperfunc.size() > 1) << "\n"; 
  // is the path segment inside a function that has a sanitization node?
  bool found = false;
  for(auto fn : pathfnames)
     if (fnames.find(fn) != fnames.end()) {
        found = true;
        break;
     }
  if (!found) return true;
  for(int i=0; i<bbperfunc.size(); i++) {
     std::vector<const BasicBlock*> *bbpath = bbperfunc[i];
     std::set<const BasicBlock*> visited;
     llvm::errs() << "Can bb " << (*(*bbpath)[0]->getTerminator()) << " reach bb " 
               << (*bbpath->back()->getTerminator()) << "\n"; 
     // we assume that dividePerFunc keeps bb's that belong to the same function
     assert((*bbpath)[0]->getParent() == bbpath->back()->getParent());
     std::vector<const BasicBlock*> p;
     if (!isReachableBB((*bbpath)[0], bbpath->back(), visited, sanbb, p)) {
        return false;
     }
  }
  return true;
}

bool InfoFlowEngine::reaches(NodeID orig, NodeID n1, std::set<NodeID> &n2set, std::set<NodeID> &visited, std::vector<NodeID> &path, 
                             std::vector<CallSiteID> &context, int depth,
                             std::vector<std::vector<NodeID>*> &localPaths, 
                             std::vector<std::vector<NodeID>*> &compPaths, NodeID &n2, bool strictValue) {
   /*if (terminateOnVisit.find(n1) != terminateOnVisit.end()) {
      llvm::errs() << "Path fails due to sanitization!\n";
      printPath(path);
      llvm::errs() << "END OF EXP FOR SANITIZATION\n"; 
      return false;
   }*/
 
   llvm::errs() << "Reaches enter...\n";
   static std::set<std::string> *sinkFuncs = NULL;
   if (!sinkFuncs) {
      sinkFuncs = new std::set<std::string>();
      for(auto n2 : n2set) {
         const Instruction *inst = getNodeInstruction(svfg->getSVFGNode(n2));
         if (inst)
            sinkFuncs->insert(inst->getParent()->getParent()->getName());
      }
   }
   std::string cst = getStringForContext(context);
   //if (n1 == n2) {
   if (n2set.find(n1) != n2set.end()) {
      n2 = n1;
      path.push_back(n1);
      return true;
   }
   if (visited.find(n1) != visited.end())
      return false;
   if (InfoFlowBound != 0 && depth > InfoFlowBound) {
      llvm::errs() << "WARNING: infoflow bound " << InfoFlowBound << " reached!\n";
      return false;
   }
   visited.insert(n1);
   
   std::vector<NodeID> *lp = NULL;
   if (localPaths.size() == 0) {
      lp = new std::vector<NodeID>();
      localPaths.push_back(lp);
   }
   else lp = localPaths.back();
   path.push_back(n1);
   lp->push_back(n1);
   SVFGNode *node = svfg->getSVFGNode(n1);
   const Instruction *n1inst = getNodeInstruction(node);
   if (!n1inst)
      llvm::errs() << "Node instruction NULL!!!!!\n";
   if (InfoFlowCtrlSan && isCtrlFlowSan(n1inst)) {
      return false;
   }
   bool summarize = InfoFlowSummarize;
   if (InfoFlowSummarize && !InfoFlowRelaxed && strictValue) {
      if (summaryMap.find(n1) != summaryMap.end()) {
         if (n1inst) { 
            std::string fn = n1inst->getParent()->getParent()->getName();
            summarize = (sinkFuncs->find(fn) == sinkFuncs->end());
            if (!summarize) 
               llvm::errs() << "Skipping summary for function " << fn << "\n";
            else llvm::errs() << "May use summary for function " << fn << "\n";   
         }
         else summarize = false;
      }    
      else summarize = false;
   }
   if (InfoFlowSummarize && (InfoFlowRelaxed || !strictValue)) {
      if (summaryMapRelaxed.find(n1) != summaryMapRelaxed.end()) {
         if (n1inst) { 
            std::string fn = n1inst->getParent()->getParent()->getName();
            summarize = (sinkFuncs->find(fn) == sinkFuncs->end());
            if (!summarize) 
               llvm::errs() << "Skipping summary for function " << fn << "\n";
            else llvm::errs() << "May use summary for function " << fn << "\n";
         }
         else summarize = false;
      }    
      else summarize = false;
   }
   if (summarize && !InfoFlowRelaxed && strictValue) {
      for(auto dest : (*summaryMap[n1])) {
         llvm::errs() << "Using summary dest " << dest << " from " << n1 << "\n";
         if (reaches(orig, dest, n2set, visited, path, context,
                  depth+1, localPaths, compPaths, n2, strictValue)) {
            /*if (!validCFL(*lp, terminateOnVisit)) {
              llvm::errs() << "Path is not CFG and sanitization consistent!\n";
              return false;
            }*/
            compPaths.push_back(lp);
            return true;
         }
      }  
   }
   else if (summarize && (InfoFlowRelaxed || !strictValue)) {
      for(auto dest : (*summaryMapRelaxed[n1])) {
         llvm::errs() << "Using summary dest " << dest << " from " << n1 << "\n";
         if (reaches(orig, dest, n2set, visited, path, context,
                  depth+1, localPaths, compPaths, n2, strictValue)) {
            /*if (!validCFL(*lp, terminateOnVisit)) {
              llvm::errs() << "Path is not CFG and sanitization consistent!\n";
              return false;
            }*/
            compPaths.push_back(lp);
            return true;
         }
      }  
   }
   else {

   llvm::errs() << "Summaries not applied!\n";

   const SVFGEdge::SVFGEdgeSetTy& outEdges = node->getOutEdges();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = outEdges.begin();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = outEdges.end();
   for (; edgeIt != edgeEit; ++edgeIt) {
       const SVFGEdge *edge = *edgeIt;
       // we stop when what is propagated is not preserved!
       if (!InfoFlowRelaxed && strictValue && orig != n1 && isDereferencingEdge(edge)) {
          //llvm::errs() << "Skipping path reachability due to dereferencing edge\n";
          continue;
       }
       /// perform context sensitive reachability
       // push context for calling
       if (edge->isCallVFGEdge()) {
          CallSiteID csId = 0;
          if (const CallDirSVFGEdge* callEdge = SVFUtil::dyn_cast<CallDirSVFGEdge>(edge))
             csId = callEdge->getCallSiteId();
          else
            csId = SVFUtil::cast<CallIndSVFGEdge>(edge)->getCallSiteId();

          context.push_back(csId);
          localPaths.push_back(new std::vector<NodeID>());
       }
      // match context for return
      else if (edge->isRetVFGEdge()) {
         CallSiteID csId = 0;
         if (const RetDirSVFGEdge* callEdge = SVFUtil::dyn_cast<RetDirSVFGEdge>(edge))
            csId = callEdge->getCallSiteId();
         else
            csId = SVFUtil::cast<RetIndSVFGEdge>(edge)->getCallSiteId();
 
         if (!context.empty()) {
            if (context.back() != csId)  {
               //llvm::errs() << "Mismatching context \n";
               return false;
            }
            else  {
               context.pop_back();
               /*if (!validCFL(*lp, terminateOnVisit)) {
                  llvm::errs() << "Path is not CFG and sanitization consistent!\n";
                  return false;
               } */
               compPaths.push_back(lp);              
               localPaths.pop_back();
            }
         }
      }

      if (reaches(orig, edge->getDstNode()->getId(), n2set, visited, path, context, 
                  depth+1, localPaths, compPaths, n2, strictValue)) {
            /*if (!validCFL(*lp, terminateOnVisit)) {
              llvm::errs() << "Path is not CFG and sanitization consistent!\n";
              return false;
            }*/
            compPaths.push_back(lp);              
            return true;
       }
     }  
   }
   path.pop_back();
   lp->pop_back();
   //llvm::errs() << "backtracking from svfg node " << n1 << "\n";
   //recordDoesnotreach(n1, cst, n2);
   return false;    
}

void InfoFlowEngine::findPointedByFrontiers(std::set<NodeID> &nids2, 
                                                  std::map<NodeID, std::set<NodeID>* > &idToFrontierSetMap, 
                                                  std::set<NodeID> &all) {
  for(auto nid2 : nids2) {
     std::set<NodeID> visited;
     llvm::errs() << "Checking frontiers for " << nid2 << "\n";
     findPointedByFrontiers(nid2, nid2, idToFrontierSetMap, all, visited);
  }
}


bool InfoFlowEngine::isDereferencingEdge(const SVFGEdge *edge) {
   NodeID srcid = edge->getSrcID();
   SVFGNode *src = svfg->getSVFGNode(srcid);
   NodeID dstid = edge->getDstID();
   SVFGNode *dst = svfg->getSVFGNode(dstid);
   StmtSVFGNode* srcstmtnode = NULL;
   StmtSVFGNode* dststmtnode = NULL;
   if ((srcstmtnode = SVFUtil::dyn_cast<StmtSVFGNode>(src)) &&
       (dststmtnode = SVFUtil::dyn_cast<StmtSVFGNode>(dst))) {
      const PAGEdge* sedge = srcstmtnode->getPAGEdge();
      const PAGEdge* dedge = dststmtnode->getPAGEdge();
      if ((SVFUtil::isa<GepPE>(sedge) &&
            (SVFUtil::isa<StorePE>(dedge) || SVFUtil::isa<LoadPE>(dedge))) 
          ||
          (SVFUtil::isa<LoadPE>(sedge) && SVFUtil::isa<LoadPE>(dedge))
         ) 
         return true;
   }
   return false;
}


void InfoFlowEngine::findPointedByFrontiers(NodeID orig, NodeID nid2,
                                                  std::map<NodeID, std::set<NodeID>* > &idToFrontierSetMap,
                                                  std::set<NodeID> &all, std::set<NodeID> &visited) {
    if (visited.find(nid2) != visited.end())
       return;
    //llvm::errs() << "Searching pointedby frontiers from " << nid2 << "\n";
    visited.insert(nid2);
    SVFGNode *node = svfg->getSVFGNode(nid2);
    const SVFGEdge::SVFGEdgeSetTy& outEdges = node->getOutEdges();
    SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = outEdges.begin();
    SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = outEdges.end();
    for (; edgeIt != edgeEit; ++edgeIt) {
       const SVFGEdge *edge = *edgeIt;
       //llvm::errs() << "is dereferencing edge " << edge->getSrcID() << "-->" << edge->getDstID() << "?\n";
       //if (isDereferencingEdge(edge)) {
       bool gep = false;
       SVFGNode *dstnode = svfg->getSVFGNode(edge->getDstID());  
       StmtSVFGNode *dststmtnode = SVFUtil::dyn_cast<StmtSVFGNode>(dstnode);
       if (dststmtnode) {
          const PAGEdge* dedge = dststmtnode->getPAGEdge();
          gep = SVFUtil::isa<GepPE>(dedge);
       }
       if (gep) {
          //llvm::errs() << "\tdereferencing edge " << edge->getSrcID() << "-->" << edge->getDstID() << "?\n";
          std::set<NodeID> *rs = NULL;
          if (idToFrontierSetMap.find(orig) == idToFrontierSetMap.end()) {
             idToFrontierSetMap[orig] = new std::set<NodeID>();
          }
          rs = idToFrontierSetMap[orig];          
          rs->insert(edge->getDstID());
          all.insert(edge->getDstID());
       }
       else { 
          //llvm::errs() << "\tsearching for those reachable from " << edge->getDstID() << "\n";
          findPointedByFrontiers(orig, edge->getDstID(), idToFrontierSetMap, all, visited);
       }
   }
 
}

Solution *InfoFlowEngine::solveForPointedBy(Label *label, Identifier *id1, Solution *sol1, Identifier *id2, Solution *sol2) {
    //llvm::errs() << "Solving for pointedby label=" << label->getDescription() << "\n";
    MemSSA *mssa = svfg->getMSSA();
    PointerAnalysis *pta = mssa->getPTA();
    SVFModule svfModule = pta->getModule();
    std::set<std::pair<Entity*,Entity*> > results;
    std::set<SVFGNode*> ns1 = sol1->getNodes();
    std::set<NodeID> nids1 = sol1->getNodeIds();
    std::map<SVFGNode*, Entity*> m1 = sol1->getNodeMap();
    std::set<SVFGNode*> ns2 = sol2->getNodes();
    std::set<NodeID> nids2 = sol2->getNodeIds();
    std::map<SVFGNode*, Entity*> m2 = sol2->getNodeMap();
    std::map<NodeID, std::set<NodeID>* > idToFrontierSetMap;
    std::set<NodeID> pbyfrontiers;
    findPointedByFrontiers(nids2, idToFrontierSetMap, pbyfrontiers);
    std::set<NodeID> resultids;
    for(auto f : pbyfrontiers) {
       resultids.insert(f);
    }
    for(auto rid : resultids) {
       //llvm::errs() << "Matching frontier " << rid << "\n";
       SVFGNode *rnode = svfg->getSVFGNode(rid);
       assert(rnode);
       GenericValueEntity *rentity = new GenericValueEntity(rnode); 
       llvm::errs() << "generic value entity: " << rentity->getStringRep() << " for node " << prettyPrint(rnode) << "\n";
       for(auto pid : nids2) {
          //llvm::errs() << "\n\nwith its pointer " << pid << " frontier set size=" << idToFrontierSetMap.size() << "\n";
          if (idToFrontierSetMap.find(pid) != idToFrontierSetMap.end()) {
             std::set<NodeID> *fs = idToFrontierSetMap[pid];
             if (fs->find(rid) == fs->end()) continue;  
             std::set<NodeID> v;
             std::vector<NodeID> path;
             std::vector<std::vector<NodeID>*> localPaths, compPaths;
             std::vector<CallSiteID> context;
             NodeID ridres = -1;
             std::set<NodeID> ridset;
             ridset.insert(rid);  
             // we do not enfoce strict value flow here as pointed by frontier allows dereferencing
             bool ctrlsan = InfoFlowCtrlSan;
             InfoFlowCtrlSan = false;
             if (reaches(pid, pid, ridset, v, path, context, 0, localPaths, compPaths, ridres, false)) {
                if (InfoFlowQueryVerbose) {
                   stats << "Explanation for pointed-by bw " << pid << " and " << rid << "\n";
                   printPath(path);
                   stats << "end of explanation\n";
                }
                SVFGNode *pnode = svfg->getSVFGNode(pid);
                assert(m2.find(pnode) != m2.end());
                Entity *pentity = m2[pnode];
                // this may not work if implicit calls are involved!!
                //if (explainDataFlowViaCF(pid, rid)) {
                results.insert(std::make_pair(rentity, pentity));            
                //} 
             }
             InfoFlowCtrlSan = ctrlsan;
          }
       }
    } 
    if (InfoFlowQueryVerbose) {
       std::string descr(" POINTED BY ");
       printResults(label, descr, results);
    }
    return new BinarySolution(label, results);
}

bool InfoFlowEngine::validCFL(std::vector<std::vector<NodeID>*> &compPaths, 
                              std::set<const BasicBlock*> &sanbb, 
                              std::set<std::string> &sanfunc) {
  //llvm::errs() << "Number of path segments=" << compPaths.size() << "\n";
  for(int i=0; i<compPaths.size(); i++) {
     //llvm::errs() << "Checking if path segment is a realizable path wrt control-flow and sanitization\n";
     if (!validCFL(*compPaths[i], sanbb, sanfunc)) {
        //printPath(*compPaths[i]);
        return false; 
     }
  }
  return true;
}



bool withinIntraProcLoop(const BasicBlock *bb) {

  for (auto it = pred_begin(bb), et = pred_end(bb); it != et; ++it) {
      const BasicBlock* pred = *it;
      std::set<const BasicBlock*> v, s;
      std::vector<const BasicBlock*> p;
      if (isReachableBB(bb, pred, v, s, p))
         return true;
  }
  return false;
}

bool matchesSVFGNodeByType(SVFGNode *node, ETYPE type, std::string tname="", int field=-1) {
   const Instruction *inst = getNodeInstruction(node);
   if (inst) {
      if (type == addrExprT) {
         if (const GetElementPtrInst *ge= llvm::dyn_cast<GetElementPtrInst>(inst))  {
            for(int i=0; i<ge->getNumOperands(); i++) {
               if (llvm::dyn_cast<Constant>(ge->getOperand(i)))
                  return false;
               if (withinIntraProcLoop(ge->getParent()))
                  return false;
            }
            return true;
         }
         else return false;
      }
      else if (type == fieldAddrExprT) {
          if (const GetElementPtrInst *ge= llvm::dyn_cast<GetElementPtrInst>(inst))  {
              llvm::errs() << "Does gep " << (*ge) << " match " << tname << " field " << field << "\n"; 
              Value *ba = ge->getOperand(0);
              if (ge->getNumOperands() >= 3 && getBaseTypeName(ba->getType()).find(tname) != std::string::npos) {
                 Value *op = ge->getOperand(2);
                 if (Constant *c = llvm::dyn_cast<Constant>(op)) {
                    return (c->getUniqueInteger().getLimitedValue() == field);
                 }
              }
          }
      }
   }
   return false;
}

Solution *InfoFlowEngine::solveForTaintedby(Label *label, Identifier *id1, Solution *sol1, Identifier *id2, Solution *sol2,
                                        Solution *sol3, std::vector<int> retval) {

    llvm::errs() << "Solving for taint label=" << label->getDescription() << "\n";
    MemSSA *mssa = svfg->getMSSA();
    PointerAnalysis *pta = mssa->getPTA();
    SVFModule svfModule = pta->getModule();
    std::set<std::pair<Entity*,Entity*> > results;
    //std::set<SVFGNode*> ns1 = sol1->getNodes();
    //std::set<NodeID> nids1 = sol1->getNodeIds();
    std::map<SVFGNode*, Entity*> m1;
    std::set<SVFGNode*> ns2 = sol2->getNodes();
    std::set<NodeID> nids2 = sol2->getNodeIds();
    std::map<SVFGNode*, Entity*> m2 = sol2->getNodeMap();
    std::set<NodeID> ns3;
    std::set<const BasicBlock*> sanbb;
    std::set<std::string> sanfunc;
    if (sol3) {
       ns3 = sol3->getNodeIds();
       filterSanitizationBB(ns3, sanbb, sanfunc, retval);
       //llvm::errs() << "Sanitization nodes for return value : " 
         //           << retval << "\n";
       for(auto si : ns3) {
          SVFGNode *spn = svfg->getSVFGNode(si);
          llvm::errs() << si << " " << prettyPrint(spn) << "\n";
       }
       llvm::errs() << "Sanitization blocks:\n";
       for(auto bb : sanbb) {
          llvm::errs() << "bb terminated by " << (*bb->getTerminator()) 
                       << " at line " << SVFUtil::getSourceLoc(bb->getTerminator()) << "\n";
       }
    }
   std::map<NodeID, std::set<NodeID> > succMap;
   for(auto n2 : nids2) {
     std::set<NodeID> v, ts, s;
     findSuccessors(n2, v, ts);
     llvm::errs() << "Successors of " << n2  << "size=" << ts.size() << "\n";
     for(auto sn : ts) {
        SVFGNode *snode = svfg->getSVFGNode(sn);
        if (matchesSVFGNodeByType(snode, id1->getType())) {
           s.insert(sn);
           m1[snode] = Entity::create(id1->getType(), snode);
        }
     }
     llvm::errs() << "Matching uccessors of " << n2  << "size=" << s.size() << "\n";
     succMap[n2] = s;
   }
   for(auto n2 : nids2) {
      SVFGNode *node2 = svfg->getSVFGNode(n2);
      std::set<NodeID> ss = succMap[n2];
      for(auto sn : ss) {
         SVFGNode *node1 = svfg->getSVFGNode(sn);
         results.insert(std::make_pair(m1[node1], m2[node2]));
      }
   }
   if (InfoFlowQueryVerbose) {
      std::string descr(" TAINTED BY ");
      printResults(label, descr, results);
   }
   BinarySolution *res = new BinarySolution(label, results);
   return res;
}


Solution *InfoFlowEngine::solveForTaint(Label *label, Identifier *id1, Solution *sol1, Identifier *id2, Solution *sol2, 
                                        Solution *sol3, std::vector<int> retval) {
    llvm::errs() << "Solving for taint label=" << label->getDescription() << "\n";
    MemSSA *mssa = svfg->getMSSA();
    PointerAnalysis *pta = mssa->getPTA();
    SVFModule svfModule = pta->getModule();
    std::set<std::pair<Entity*,Entity*> > results;
    std::set<SVFGNode*> ns1 = sol1->getNodes();
    std::set<NodeID> nids1 = sol1->getNodeIds();
    std::map<SVFGNode*, Entity*> m1 = sol1->getNodeMap();
    std::set<SVFGNode*> ns2 = sol2->getNodes();
    std::set<NodeID> nids2 = sol2->getNodeIds();
    std::map<SVFGNode*, Entity*> m2 = sol2->getNodeMap();
    std::set<NodeID> ns3;
    std::set<const BasicBlock*> sanbb;
    std::set<std::string> sanfunc;
    if (sol3) {
       ns3 = sol3->getNodeIds();
       filterSanitizationBB(ns3, sanbb, sanfunc, retval);
       //llvm::errs() << "Sanitization nodes: " << retval << "\n";
       for(auto si : ns3) {
          SVFGNode *spn = svfg->getSVFGNode(si);
          llvm::errs() << si << " " << prettyPrint(spn) << "\n";
       }
       llvm::errs() << "Sanitization blocks:\n";
       for(auto bb : sanbb) {
          llvm::errs() << "bb terminated by " << (*bb->getTerminator()) 
                       << " at line " << SVFUtil::getSourceLoc(bb->getTerminator()) << "\n";
       }
    }
    /*
    std::map<std::string, std::set<Entity*>*> cl1 = Entity::generateClusters(sol1->getResults());
    stats << "Number of clusters for " << id1->getName() << "=" << cl1.size() << "\n";  
    llvm::errs() << "Number of clusters for " << id1->getName() << "=" << cl1.size() << "\n";  
    for(auto clp : cl1)  {
        llvm::errs() << "cluster " << clp.first << "\n";
        stats << "cluster " << clp.first << "\n";
    }
    std::map<std::string, std::set<Entity*>*> cl2 = Entity::generateClusters(sol2->getResults()); 
    stats << "Number of clusters for " << id2->getName() << "=" << cl2.size() << "\n";  
    llvm::errs() << "Number of clusters for " << id2->getName() << "=" << cl2.size() << "\n";  
    for(auto clp : cl2) { 
        llvm::errs() << "cluster " << clp.first << "\n";
        stats << "cluster " << clp.first << " size=" << clp.second->size() << "\n";
    }*/
    llvm::errs() << id1->getName() << " size=" << nids1.size() << "\n";
    llvm::errs() << id2->getName() << " size=" << nids2.size() << "\n";
    stats << id1->getName() << " size=" << nids1.size() << "\n";
    stats << id2->getName() << " size=" << nids2.size() << "\n";
    stats.flush();
    if (FLOWANALYZER) {
       FlowAnalyzer *fl = new FlowAnalyzer(ns1, ns2, svfg);
       std::set<std::pair<NodeID,NodeID> > pairs;
       fl->initialize();
       fl->analyze(pairs, ns3);
       for(auto p : pairs) {
          SVFGNode *src = svfg->getSVFGNode(p.first);
          SVFGNode *snk = svfg->getSVFGNode(p.second);
          results.insert(std::make_pair(m1[src], m2[snk]));
       }
       BinarySolution *res = new BinarySolution(label, results);
       return res;  
    }
 
  if (InfoFlowExhaustive) {
    for(auto nid1 : nids1) {
        SVFGNode *n1 = svfg->getSVFGNode(nid1);
        for(auto nid2 : nids2) {
              SVFGNode *n2 = svfg->getSVFGNode(nid2);
              std::set<NodeID> v;
              std::vector<NodeID> path; 
              std::vector<std::vector<NodeID>*> localPaths, compPaths;
              std::vector<CallSiteID> context;
              llvm::errs() << "Checking if " << n1->getId() << " reaches " << n2->getId() << "\n";
              std::set<NodeID> destset;
              destset.insert(nid2);
              NodeID dest;
              if (reaches(nid1, nid1, destset, v, path, context, 0, localPaths, compPaths, dest, true))  {
                 llvm::errs() << "isavalidcfp?\n";
                 if (!validCFL(compPaths, sanbb, sanfunc)) continue;
                 llvm::errs() << "Yes!, path:\n";
                 if (const Instruction *ii = getNodeInstruction(n1))
                    stats << "Source node=" << nid1 << " Inst1=" << getInstructionStr(ii)
                          << " Loc=" << SVFUtil::getSourceLoc(ii) << "\n";
                 if (const Instruction *ii = getNodeInstruction(n2))
                    stats << "Dest node=" << nid2 << " Inst2=" << getInstructionStr(ii)
                          << " Loc=" << SVFUtil::getSourceLoc(ii)<< "\n";
                 stats << "Path from " << nid1 << " " << nid2 << "\n";
                 if (InfoFlowVerbose)  
                    stats << "\n====================================\n";
                 for(int i=0; i<path.size(); i++) {
                    SVFGNode *spn = svfg->getSVFGNode(path[i]);
                    llvm::errs() << "(" << path[i] << ")" << prettyPrint(spn) << ", ";
                    if (InfoFlowVerbose)  {
                       stats << prettyPrint(spn) << " " << ",";
                    }
                 }
                 if (InfoFlowVerbose)  
                    stats << "\n====================================\n";
                 llvm::errs() << "\n"; 
                 results.insert(std::make_pair(m1[n1], m2[n2]));
              }
        }
    }
   }
   else {
    for(auto nid1 : nids1) {
        SVFGNode *n1 = svfg->getSVFGNode(nid1);
        std::set<NodeID> v;
        std::vector<NodeID> path;
        std::vector<std::vector<NodeID>*> localPaths, compPaths;
        std::vector<CallSiteID> context;
        NodeID nid2 = -1;
        if (reaches(nid1, nid1, nids2, v, path, context, 0, localPaths, compPaths, nid2, true))  {
           assert(nid2 != -1);
           llvm::errs() << "isavalidcfp?\n";
           if (!validCFL(compPaths, sanbb, sanfunc)) continue;
           llvm::errs() << "Yes!, path:\n";
           if (const Instruction *ii = getNodeInstruction(n1))
              stats << "Source node=" << nid1 << " Inst1=" << getInstructionStr(ii)
                    << " Loc=" << SVFUtil::getSourceLoc(ii) << "\n";
           printPath(path);
           SVFGNode *n2 = svfg->getSVFGNode(nid2);
           results.insert(std::make_pair(m1[n1], m2[n2]));
        }
    }
   }  
    BinarySolution *res = new BinarySolution(label, results);
    return res;  
}


Solution *InfoFlowEngine::solveForCallbackDefine(Label *label, Identifier *id1, Solution *sol1, 
                                                               Identifier *id2, Solution *sol2) {
    BinarySolution *res = sol1->filterDefinesCallback(label, sol2);
    return res;
}

void InfoFlowEngine::findAddrNodes(std::set<NodeID> &ns, std::set<NodeID> &addrnodes,
                  std::map<NodeID, std::set<NodeID> > &am/*, std::map<NodeID, std::set<const Function*> > &funcm, 
                  std::map<const Function*, std::set<NodeID> > &ftonmap*/) {
  for(auto n : ns) {
      std::set<NodeID> visited;
      findAddrNodes(n, n, visited, addrnodes, am/*, funcm, ftonmap*/); 
  }
}

void InfoFlowEngine::findAddrNodes(NodeID o, NodeID n, std::set<NodeID> &visited, std::set<NodeID> &addrnodes, 
                  std::map<NodeID, std::set<NodeID> > &am/*, std::map<NodeID, std::set<const Function*> > &funcmap,
                  std::map<const Function*, std::set<NodeID> > &ftonmap*/) {
  if (visited.find(n) != visited.end())
     return;
  visited.insert(n);
  SVFGNode *node = svfg->getSVFGNode(n);
  if (StmtSVFGNode* stmtNode = SVFUtil::dyn_cast<StmtSVFGNode>(node)) {
     const PAGEdge* edge = stmtNode->getPAGEdge();
     if (SVFUtil::isa<AddrPE>(edge)) {
        addrnodes.insert(n);
        std::set<NodeID> s;
        if (am.find(n) != am.end())
           s = am[n];
        s.insert(o);
        am[n] = s;
        return;
     } 
  }
  const SVFGEdge::SVFGEdgeSetTy& inEdges = node->getInEdges();
  SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = inEdges.begin();
  SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = inEdges.end();
  for (; edgeIt != edgeEit; ++edgeIt) {
      const SVFGEdge *edge = *edgeIt;  
      findAddrNodes(o, edge->getSrcID(), visited, addrnodes, am/*, funcmap, ftonmap*/);
  } 
}

void InfoFlowEngine::findSuccessors(NodeID n, std::set<NodeID> &visited, std::set<NodeID> &succs) {

   if (visited.find(n) != visited.end())
      return;
   visited.insert(n);
   succs.insert(n);
   SVFGNode *node = svfg->getSVFGNode(n);
   const SVFGEdge::SVFGEdgeSetTy& outEdges = node->getOutEdges();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = outEdges.begin();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = outEdges.end();
   for (; edgeIt != edgeEit; ++edgeIt) {
       const SVFGEdge *edge = *edgeIt;
       findSuccessors(edge->getDstNode()->getId(), visited, succs);
   }
}

void InfoFlowEngine::findPredecessors(NodeID n, std::set<NodeID> &visited, std::set<NodeID> &preds) {

   if (visited.find(n) != visited.end())
      return;
   visited.insert(n);
   SVFGNode *node = svfg->getSVFGNode(n);
   const Instruction *n1inst = getNodeInstruction(node);
   if (InfoFlowSummarize && summaryMapRelaxedReverse.find(n) != summaryMapRelaxedReverse.end()) {
      preds.insert(n);
      for(auto src : (*summaryMapRelaxedReverse[n])) {
         llvm::errs() << "Using summary pred " << src << " from " << n << "\n";
         findPredecessors(src, visited, preds);
      }
      return;
   }
   llvm::errs() << "Using exact pred " << n << "\n";
   preds.insert(n);
   const SVFGEdge::SVFGEdgeSetTy& inEdges = node->getInEdges();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = inEdges.begin();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = inEdges.end();
   for (; edgeIt != edgeEit; ++edgeIt) {
       const SVFGEdge *edge = *edgeIt;
       findPredecessors(edge->getSrcNode()->getId(), visited, preds);
   }
}

bool InfoFlowEngine::explainDataFlowViaCF(NodeID n1, NodeID n2) {
   SVFGNode *node1 = svfg->getSVFGNode(n1);
   const Instruction *inst1 = getNodeInstruction(node1);  
   SVFGNode *node2 = svfg->getSVFGNode(n2);
   const Instruction *inst2 = getNodeInstruction(node2);  
   if (inst1 && inst2) {
      if (inst1->getParent()->getParent() == inst2->getParent()->getParent()) {
         std::set<const BasicBlock*> visited, sanbb;
         std::vector<const BasicBlock*> rp;
         llvm::errs() << "Checking intra cfl from " << n1 << " to " << n2 << "\n";
         if (isReachableBB(inst1->getParent(), inst2->getParent(), visited, sanbb, rp))  {
            llvm::errs() << "Explaining dataflow path from " << n1 << " to " << n2 << "\n";
            printPath(rp);
            llvm::errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
            return true;
         }
      }
      else {
         ICFGNode *rnode1 = getICFGNode(inst1->getParent());
         ICFGNode *rnode2 = getICFGNode(inst2->getParent());
         std::set<const ICFGNode*> visited, rsanbb;
         std::vector<NodeID> rp;
         std::vector<CallSiteID> context;
         llvm::errs() << "Checking inter cfl from " << n1 << " to " << n2 << "\n";
         if (isReachableICFG(rnode1, rnode2, visited, rsanbb, rp, context)) {
            //llvm::errs() << "Explaining dataflow path from " << n1 << " to " << n2 << "\n";
            //printICFGPath(rp, node1, node2);
            //llvm::errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
            return true;
         }
      }
   }
   return false; 
}

void print(const PointsTo &ps) {
  std::string str = "pts{";
  for (PointsTo::iterator ii = ps.begin(), ie = ps.end();
                ii != ie; ii++) {
            char int2str[16];
            sprintf(int2str, "%d", *ii);
            str += int2str;
            str += " ";
        }
  str += "} \n";
  llvm::errs() << str << "\n";
}

Solution *InfoFlowEngine::solveForTaintBothInSeq(Label *label, Identifier *id1, Solution *sol1,
                                          Identifier *id2, Solution *sol2, Identifier *id3, Solution *sol3, 
                                          Solution *sol4, std::vector<int> retval) {
   std::set<NodeID> nid1 = sol1->getNodeIds();
   std::map<SVFGNode*, Entity*> m1 = sol1->getNodeMap();
   std::set<NodeID> nid2 = sol2->getNodeIds();
   std::map<SVFGNode*, Entity*> m2 = sol2->getNodeMap();
   std::set<NodeID> nid3 = sol3->getNodeIds();
   std::map<SVFGNode*, Entity*> m3 = sol3->getNodeMap();
   std::set<NodeID> ns4;
   std::set<const BasicBlock*> sanbb;
   std::set<std::string> sanfunc;
   std::set<std::tuple<Entity*,Entity*,Entity*> > results;
   MemSSA *mssa = svfg->getMSSA();
   if (sol4) {
       ns4 = sol4->getNodeIds();
       filterSanitizationBB(ns4, sanbb, sanfunc, retval);
       //llvm::errs() << "Sanitization nodes: " << retval << "\n";
       for(auto si : ns4) {
          SVFGNode *spn = svfg->getSVFGNode(si);
          llvm::errs() << si << " " << prettyPrint(spn) << "\n";
       }
       llvm::errs() << "Sanitization blocks:\n";
       for(auto bb : sanbb) {
          llvm::errs() << "bb terminated by " << (*bb->getTerminator()) 
                       << " at line " << SVFUtil::getSourceLoc(bb->getTerminator()) << "\n";
       }
   }
   std::map<NodeID, std::set<NodeID> > predMap;
   for(auto n2 : nid2) {
     std::set<NodeID> v, p;
     llvm::errs() << "Finding predecessors of " << n2 << "\n";
     findPredecessors(n2, v, p);
     predMap[n2] = p;
   }     
   for(auto n3 : nid3) {
     std::set<NodeID> v, p;
     llvm::errs() << "Finding predecessors of " << n3 << "\n";
     findPredecessors(n3, v, p);
     predMap[n3] = p;
   }     
   for(auto n2 : nid2) {
      if (predMap.find(n2) == predMap.end())
         continue;
      SVFGNode *node2 = svfg->getSVFGNode(n2);
      /*PointsTo pset2;
      if (const ActualINSVFGNode *anode2 = SVFUtil::dyn_cast<ActualINSVFGNode>(node2)) {
         const PointsTo &pts2 = anode2->getPointsTo();
         pset2 = pts2;
      }
      else if (const ActualParmSVFGNode *apn2 =  SVFUtil::dyn_cast<ActualParmSVFGNode>(node2)) {
         MRGenerator::MRSet &mrs = mssa->getMRGenerator()->getCallSiteRefMRSet(apn2->getCallSite());
         for(auto mr : mrs) {
            const PointsTo &pts = mr->getPointsTo();
            print(pts);
            pset2 |= pts;
         }
      }
      else llvm::errs() << "skipping due to not being ana actual parameter node\n";*/
      const Instruction *inst2 = getNodeInstruction(node2);      
      std::set<NodeID> s2 = predMap[n2];
      for(auto n3 : nid3) {
         if (n2 == n3) continue;
         if (predMap.find(n3) == predMap.end())
            continue;
         SVFGNode *node3 = svfg->getSVFGNode(n3);
         if (getNodeInstruction(node2) == getNodeInstruction(node3)) continue;
         /*PointsTo pset3;
         if (const ActualINSVFGNode *anode3 = SVFUtil::dyn_cast<ActualINSVFGNode>(node3)) {
            const PointsTo &pts3 = anode3->getPointsTo();
            pset3 = pts3;
         }
         else if (const ActualParmSVFGNode *apn3 =  SVFUtil::dyn_cast<ActualParmSVFGNode>(node3)) {
            MRGenerator::MRSet &mrs = mssa->getMRGenerator()->getCallSiteRefMRSet(apn3->getCallSite());
            for(auto mr : mrs) {
               const PointsTo &pts = mr->getPointsTo();
               print(pts);
               pset3 |= pts;
            }
         }
         else llvm::errs() << "skipping due to not being ana actual parameter node\n";
         print(pset2);
         print(pset3);
         if (!(pset2 == pset3)) {
         //if (!(pset2.intersects(pset3))) {
            llvm::errs() << "skipping due to different points to sets: \n";
            continue;
         }*/
         const Instruction *inst3 = getNodeInstruction(node3);      
         std::set<NodeID> s3 = predMap[n3]; 
         std::set<NodeID> inter;
         set_intersection(begin(s2), end(s2), begin(s3), end(s3), 
                          inserter(inter, end(inter)));
         llvm::errs() << "checking common taint source for " << n2 << " and " << n3 << "?\n";
         llvm::errs() << "pred sizes " << s2.size() << " and " << s3.size() << "\n";
         if (inter.size() > 0) {
            /*bool found = false;
            for(auto ints : inter) {
               if (ints == 0) continue;
               llvm::errs() << "common taint source for " << n2 << " and " << n3 << " is " << ints << "\n";
               if (found) break;
               std::set<NodeID> v1, v2;
               std::vector<NodeID> path1, path2;
               std::vector<std::vector<NodeID>*> localPaths1, localPaths2, compPaths1, compPaths2;
               std::vector<CallSiteID> context1, context2;
             */
               //if (reaches(ints, n2, v1, path1, context1, 0, localPaths1, compPaths1, false) && 
               //    reaches(ints, n3, v2, path2, context2, 0, localPaths2, compPaths2, false)
               //   ) {

                  if (inst2->getParent()->getParent() == inst3->getParent()->getParent()) { 
                     if (SkipIntraResults)
                        continue;
                     std::set<const BasicBlock*> visited;
                     llvm::errs() << "Checking cf path from " << n2 <<  " to " << n3 << "\n";
                     llvm::errs() << (*inst2->getParent()->getTerminator()) << " to "
                            << (*inst3->getParent()->getTerminator()) << "\n";
                     std::vector<const BasicBlock*> rp;
                     //if (inst2->getParent() == inst3->getParent()) continue;
                     if (isReachableBB(inst2->getParent(), inst3->getParent(), visited, sanbb, rp)) {
                        //SVFGNode *node1 = svfg->getSVFGNode(ints);
                        //const Instruction *inst1 = getNodeInstruction(node1);
                        assert(m2.find(node2) != m2.end());
                        assert(m3.find(node3) != m3.end());
                        std::set<const BasicBlock*> visited2;
                        std::vector<const BasicBlock*> rp2;
                        if (InfoFlowExcludeLoops && 
                            isReachableBB(inst3->getParent(), inst2->getParent(), visited2, sanbb, rp2)) {
                           llvm::errs() << "Skipping pairs due to being in a loop!\n";
                           continue; 
                        }
                        llvm::errs() << "taintboth in seq, n1=" //<< node1->getId()
                          << " n2=" << node2->getId() << " n3=" << node3->getId() << "\n";
                        bool typecheck = true;
                        std::string s1 = "";
                        std::string s2 = "";
                        if (InfoFlowTypeCheckEnforce) {
                           s1 = findType(svfg, node2);
                           s2 = findType(svfg, node3);
                           //if (s1 != "" && s2 != "") {
                              typecheck = (s1 != "" && s2 != "" && (s1 == s2));
                              llvm::errs() << "In typecheck: " << s1 << " vs " << s2 << "\n";
                           //}
                        }
                        llvm::errs() << "Typecheck=" << typecheck << "\n";
                        if (typecheck)  {
                           //llvm::errs() << "Typecheck=" << typecheck << " pair passes type check\n";
                           stats << s1 << " vs " << s2 << "\n";
                           if (const Instruction *ii = getNodeInstruction(node2))
                               stats << "Node1=" << n2 << " Inst1=" << getInstructionStr(ii) 
                                     << " in " << ii->getParent()->getParent()->getName().str()  
                                     << " Loc1=" << SVFUtil::getSourceLoc(ii) << "\n";
                           if (const Instruction *ii = getNodeInstruction(node3))
                               stats << "Node2=" << n3 << " Inst2=" << getInstructionStr(ii) 
                                     << " in " << ii->getParent()->getParent()->getName().str() 
                                     << " Loc2=" << SVFUtil::getSourceLoc(ii) << "\n";                             
                           printPath(rp);
                           results.insert(std::make_tuple(new GenericValueEntity(), m2[node2], m3[node3]));
                          //found = true;
                        }
                     }
                  }
                  else {
                    ICFGNode *rnode2 = getICFGNode(inst2->getParent());
                    ICFGNode *rnode3 = getICFGNode(inst3->getParent());
                    std::set<const ICFGNode*> visited, rsanbb;
                    for(auto sbb : sanbb) {
                       ICFGNode *rs = getICFGNode(sbb);
                       llvm::errs() << "transforming san bb starts with " 
                                    << (*sbb->begin()) << " ends with " 
                                    << (*sbb->getTerminator()) << "\n";
                       if (rs) {
                          llvm::errs() << "sanitization node in ICFG: " << rs->getId()  << "\n";
                          rsanbb.insert(rs);
                       }
                    }
                    llvm::errs() << "Checking if icfg node " << rnode2->getId()  << prettyPrint(rnode2) 
                               << " can reach icfg node " 
                               << rnode3->getId() << prettyPrint(rnode3) << "\n";
                    std::vector<NodeID> rp;
                    std::vector<CallSiteID> context;
                    if (isReachableICFG(rnode2, rnode3, visited, rsanbb, rp, context)) {
                       //SVFGNode *node1 = svfg->getSVFGNode(n2);
                       //const Instruction *inst1 = getNodeInstruction(node2);
                       assert(m2.find(node2) != m2.end());
                       assert(m3.find(node3) != m3.end());
                       std::vector<NodeID> rp2;
                       std::set<const ICFGNode*> visited2;
                       if (InfoFlowExcludeLoops &&
                           isReachableICFG(rnode3, rnode2, visited2, rsanbb, rp2, context)) {
                           llvm::errs() << "Skipping pairs due to being in a loop!\n";
                           continue;
                       }
                       llvm::errs() << "taintboth in seq, n1=" //<< node1->getId()
                             << " n2=" << node2->getId() << " n3=" << node3->getId() << "\n";                 
                       bool typecheck = true;
                       std::string s1 = "";
                       std::string s2 = "";
                       if (InfoFlowTypeCheckEnforce) {
                          s1 = findType(svfg, node2);
                          s2 = findType(svfg, node3);
                          //if (s1 != "" && s2 != "") {
                             typecheck = (s1 != "" && s2 != "" && (s1 == s2));
                             llvm::errs() << "In typecheck: " << s1 << " vs " << s2 << "\n";
                          //}
                       }
                       llvm::errs() << "Typecheck=" << typecheck << "\n";
                       if (typecheck)  {
                          stats << s1 << " vs " << s2 << "\n";
                          if (const Instruction *ii = getNodeInstruction(node2))
                             stats << "Node1=" << n2 << " Inst1=" << getInstructionStr(ii) 
                                   << " Loc1=" << SVFUtil::getSourceLoc(ii) << "\n";
                          if (const Instruction *ii = getNodeInstruction(node3))
                             stats << "Node2=" << n3 << " Inst2=" << getInstructionStr(ii) 
                                   << " Loc2=" << SVFUtil::getSourceLoc(ii)<< "\n";
                          printICFGPath(rp, node2, node3);
                          results.insert(std::make_tuple(new GenericValueEntity(), m2[node2], m3[node3]));
                       }
                       //found = true;
                    }
                 }
              //}               
            //}
         }
      }    
   }

   TernarySolution *res = new TernarySolution(label, results);
   return res;
}

Solution *InfoFlowEngine::solveForReaches(Label *label, Identifier *id1, Solution *sol1,
                                          Identifier *id2, Solution *sol2, Solution *sol3, std::vector<int> retval) {
   std::set<NodeID> nid1 = sol1->getNodeIds();
   std::map<SVFGNode*, Entity*> m1 = sol1->getNodeMap();
   std::set<NodeID> nid2 = sol2->getNodeIds();
   std::map<SVFGNode*, Entity*> m2 = sol2->getNodeMap();
   std::set<std::pair<Entity*,Entity*> > results;
   for(auto n1 : nid1) {
      SVFGNode *node1 = svfg->getSVFGNode(n1);
      const Instruction *inst1 = getNodeInstruction(node1);
      if (!inst1) continue;
      for(auto n2 : nid2) {
          SVFGNode *node2 = svfg->getSVFGNode(n2);
          const Instruction *inst2 = getNodeInstruction(node2);
          if (!inst2) continue;
          std::set<const BasicBlock*> visited;
          int sanretval = -1;
          std::set<NodeID> sanitizers;
          std::set<const BasicBlock*> sanbb;
          std::set<std::string> fnames;
          if (sol3)  {
             sanitizers = sol3->getNodeIds();
             filterSanitizationBB(sanitizers, sanbb, fnames, retval);
          }
          std::vector<const BasicBlock*> p;
          if (isReachableBB(inst1->getParent(), inst2->getParent(), visited, sanbb, p)) {
             assert(m1.find(node1) != m1.end());
             assert(m2.find(node2) != m2.end());
             printPath(p);
             results.insert(std::make_pair(m1[node1], m2[node2]));          
          }
      }
   }
}

void InfoFlowEngine::solveRegExpForType(RegExpQueryExp *rge, Identifier *id, std::set<Entity*> &ide, 
                                         std::map<std::string, Constraint*> &constraints) {
  std::set<Entity*> *eset = NULL;
  std::set<Entity*> idet, tfeset;
  //llvm::errs() << "id type=" << id->getType() << "\n";
  if (id->getType() == structT) 
     findStructEntity(eset);
  else if (id->getType() == primitivedatafieldT)
     findDataFieldEntity(eset, false);
  else if (id->getType() == pointerdatafieldT)
     findDataFieldEntity(eset, true);
 else if (id->getType() == funcptrfieldT)
     findFuncPtrFieldEntity(eset);
  else if (id->getType() == functionT)
     findFunctionEntity(eset);
  else if (id->getType() == callsiteT)
     findCallsiteEntity(eset);
  else if ((id->getType() == globalVarT) ||
           (id->getType() == primitiveGlobalVarT) || 
           (id->getType() == pointerGlobalVarT) ||
           (id->getType() == structGlobalVarT) ||
           (id->getType() == funcptrGlobalVarT))
      findGlobalVarEntities(eset);
  else if ((id->getType() == funcArgT) ||
           (id->getType() == primitiveFuncArgT) ||
           (id->getType() == pointerFuncArgT) ||
           (id->getType() == funcptrFuncArgT) || 
           (id->getType() == structFuncArgT)) {
      //findFunctionArgEntity(eset);
      llvm::errs() << "Function arg exp\n";
  }
  else if (id->getType() == genericValueT) 
       llvm::errs() << "generic value expression\n";
    // keep it empty as it will be later filled in a pointedby relational query
  else if (id->getType() == addrExprT)
       llvm::errs() << "address expression\n";
  else if (id->getType() == fieldAddrExprT) 
       findMatchingGepForDataField(id, ide); // direct processing
  //else if (id->getType() == othertypes..
  else assert(0 && "Cannot find base entitties for invalid entity type\n!");   
  // filter based on types
  if (!eset) return;
  for(auto es : (*eset)) {
     // either not type specific or matches the desired type
     if (!((id->getType() == primitivedatafieldT) || (id->getType() == pointerdatafieldT) ||
           (id->getType() == funcptrfieldT) || (id->getType() == primitiveGlobalVarT) || 
           (id->getType() == pointerGlobalVarT) || (id->getType() == structGlobalVarT) || 
           (id->getType() == funcptrGlobalVarT) || (id->getType() == primitiveFuncArgT) || 
           (id->getType() == pointerFuncArgT) || (id->getType() == funcptrFuncArgT) || 
           (id->getType() == structFuncArgT)
          ) 
          || 
          (id->getType() == es->getType())
        )
          tfeset.insert(es);
  }
     
  // filter based on regular expression
  for(auto es : tfeset)
     if (rge->matches(es->getStringRep())) {
        //llvm::errs() << "Including " << es->toString() << " in the candidate solutions\n";
        idet.insert(es);
     }
  std:set<Entity*> disqualify;
  for(auto cm : constraints) {
     if (cm.first == id->getName()) {
        Solution *sol = solve(cm.second->getLabel());
        for(auto e : idet)
           if (!sol->isAMember(e)) {
              //llvm::errs() << " Entity " << e->toString() << " is disqualified!\n";
              disqualify.insert(e); 
           } 
     }
  }
  for(auto e : idet)
     if (disqualify.find(e) == disqualify.end())
        ide.insert(e);
}

Solution *InfoFlowEngine::solveForType(Label *label) {
  QueryExp *qexp = label->getQueryExp();
  std::map<std::string, Constraint*> constraints = label->getQuery()->getConstraints();
  if (qexp->getType() == relationalexp) {
      llvm::errs() << "Solving relation query label " << label->getDescription() << "\n";
      std::set<Entity*> ide1, ide2;
      RelationalQueryExp *relqexp = (RelationalQueryExp*)qexp;
      Identifier *id1 = relqexp->getOperand1();
      Solution *sol1 = NULL;
      if (constraints.find(id1->getName()) != constraints.end())
         sol1 = solve(constraints[id1->getName()]->getLabel());
      Identifier *id2 = relqexp->getOperand2();
      Solution *sol2 = NULL;
      Solution *bs = NULL;
      if (constraints.find(id2->getName()) != constraints.end())
         sol2 = solve(constraints[id2->getName()]->getLabel());
      if (relqexp->getRelationType() == taintedby) {
         Constraint *sanc = label->getQuery()->getSanitizationConstraint();
         Solution *sol3 = NULL;
         if (sanc) {
            sol3 = solve(sanc->getLabel());
         }
         /*Solution *tbs = solveForTaint(label, id2, sol2, id1, sol1, sol3);   
         bs = ((BinarySolution*)tbs)->reverse();
         bs->setLabel(label);*/
         bs = solveForTaintedby(label, id1, sol2, id2, sol2, sol3, label->getQuery()->getSanitizationReturnValue()); 
      }
      else if (relqexp->getRelationType() == taints) {
         Constraint *sanc = label->getQuery()->getSanitizationConstraint();
         Solution *sol3 = NULL;
         if (sanc) {
            sol3 = solve(sanc->getLabel());
         }
         llvm::errs() << "Solving " << id1->getName() << " taints " << id2->getName() << "\n";
         bs = solveForTaint(label, id1, sol1, id2, sol2, sol3, label->getQuery()->getSanitizationReturnValue());
      }
      else if (relqexp->getRelationType() == callbackdefinedin) {
         Solution *tbs = solveForCallbackDefine(label, id2, sol2, id1, sol1);
         bs  = ((BinarySolution*)tbs)->reverse();
         bs->setLabel(label);
      }
      else if (relqexp->getRelationType() == definescallback) {
         bs = solveForCallbackDefine(label, id1, sol1, id2, sol2);
      }
      else if (relqexp->getRelationType() == actualparameterpassedby) {
         bs = solveForActualparameterpassedby(label, id1, sol1, id2, sol2);
      }
      else if (relqexp->getRelationType() == actualparameterpasses) {
         Solution *tbs = solveForActualparameterpassedby(label, id2, sol2, id1, sol2);
         bs = ((BinarySolution*)tbs)->reverse();
         bs->setLabel(label);
      }      
      else if (relqexp->getRelationType() == indirectcallof) {
         bs = solveForIndirectCallOf(label, id1, sol1, id2, sol2);

      } 
      else if (relqexp->getRelationType() == pointedby) {
        llvm::errs() << "Solving " << id1->getName() << " pointedby " << id2->getName() << "\n";
        bs = solveForPointedBy(label, id1, sol1, id2, sol2);
        /*
        std::map<std::string, std::set<Entity*>*> cl = Entity::generateClusters(bs->getResults());
        stats << "Number of clusters for " << id1->getName() << "=" << cl.size() << "\n";
        llvm::errs() << "Number of clusters for " << id1->getName() << "=" << cl.size() << "\n";
        for(auto clp : cl)  {
           llvm::errs() << "cluster " << clp.first << " size=" << clp.second << "\n";
           stats << "cluster " << clp.first << " size=" << clp.second << "\n";
        }*/
      }
      else if (relqexp->getRelationType() == reachesRel) {
         Constraint *sanc = label->getQuery()->getSanitizationConstraint();
         Solution *sol3 = NULL;
         if (sanc) {
            sol3 = solve(sanc->getLabel());
         }
         bs = solveForReaches(label, id1, sol1, id2, sol2, sol3, label->getQuery()->getSanitizationReturnValue());
      }
      else if (relqexp->getRelationType() == taintsbothinseq) {
         Identifier *id3 = relqexp->getOperand3();
         Solution *sol3 = NULL;
         if (constraints.find(id3->getName()) != constraints.end())
            sol3 = solve(constraints[id3->getName()]->getLabel());
         Constraint *sanc = label->getQuery()->getSanitizationConstraint();
         Solution *sol4 = NULL;
         if (sanc) {
            sol4 = solve(sanc->getLabel());
         }
         return solveForTaintBothInSeq(label, id1, sol1, id2, sol2, id3, sol3, sol4, 
                                       label->getQuery()->getSanitizationReturnValue());
      }
      // others
      return bs;
  }
  else if (qexp->getType() == regexp) {
     llvm::errs() << "Solving regexpression query label " << label->getDescription() << "\n";
     std::set<Entity*> ide;
     RegExpQueryExp *rge = (RegExpQueryExp*)qexp;
     Identifier *id = rge->getOperand();
     solveRegExpForType(rge, id, ide, constraints);
     Solution *sol = new UnarySolution(label, ide);
     sol->print();      
     return sol;
  } 
  else assert(0 && "Invalid query for a label!\n");
  return NULL;
}
    
Solution *InfoFlowEngine::solve(Label *label) {
  if (solutionCache.find(label->getDescription()) == solutionCache.end()) {
     time_t beforeSol, afterSol, diff;
     time(&beforeSol);
     Solution *sol = solveForType(label);
     time(&afterSol);
     diff = difftime(afterSol, beforeSol);
     stats <<  "Label=" << label->getDescription() << " solution size=" << sol->getSize() 
           << " time=" << diff << "\n";
     stats.flush();
     solutionCache[label->getDescription()] = sol;
     totalSolTime += diff;
     if (sol) {
        llvm::errs() << "DEBUG: Solution for " << label->getDescription() << "\n"; 
        sol->print();
     }
  }
  else {
     numCachedSol++;
  }
  numSol++;
  return solutionCache[label->getDescription()];
  
}

void InfoFlowEngine::finalizeStats() {
  //stats << "Total number of top level unreachability decisions= " << unreach << "\n";
  //stats << "Total number of intermediate unreachability decisions= " << intermediateUnreach << "\n";
  //stats << "Total number of top level reachability decisions= " << reach << "\n";
  //stats << "Total number of intermediate reachability decisions= " << intermediateReach << "\n";
  stats << "Total number of solutions=" << numSol << "\n";
  stats << "Total number of cached solutions=" << numCachedSol << "\n";
  stats << "Total query solving time=" << totalSolTime << "\n";
  if (numSol > 0)
     stats << "Average query solving time=" << (totalSolTime/numSol) << "\n";
  int N=0, E=0;
  getSVFGStats(svfg, N, E);
  stats << "SVFG stats: nodes=" << N << " edges=" << E << "\n";
  stats.close();
}

void InfoFlowEngine::addAllActualParameterNodes() {
    /*int numadditions = 0;
    PAG* pag = PAG::getPAG();
    for(PAG::CSToArgsListMap::iterator it = pag->getCallSiteArgsMap().begin(),
            eit = pag->getCallSiteArgsMap().end(); it!=eit; ++it) {
       PAG::PAGNodeList& arglist = it->second;
       const PAGNode* pagNode = arglist.front();
       svfg->addActualParmVFGNode(pagNode,it->first);
       if (svfg->PAGNodeToDefMap.find(pagNode) != svfg->PAGNodeToDefMap.end() && 
           svfg->PAGNodeToActualParmMap.find(std::make_pair(pagNode->getId(),it->first)) != 
                                 svfg->PAGNodeToActualParmMap.end()) {
          numadditions++;
          svfg->addIntraDirectVFEdge(svfg->getDefSVFGNode(pagNode)->getId(),
                                       svfg->getActualParmVFGNode(pagNode,it->first)->getId()); 
       }
    }
    llvm::errs() << "Number of actual parameter nodes added to the graph: " << numadditions << "\n";
    */
}


Constraint *InfoFlowEngine::generateRegExpCons(ETYPE type, const char *descp, 
                                                 std::vector<const char*> *orincludes) {
   std::string desc(descp);
   RegExpQueryExp *qexp = new RegExpQueryExp(desc, type);
   for(int i=0; orincludes && i<orincludes->size(); i++) {
      std::string  s((*orincludes)[i]);
      qexp->addOrIncludes(s);
      std::string l = toLower(s);
      //llvm::errs() << "adding lower " << l << "\n";
      qexp->addOrIncludes(l);
      std::string u = toUpper(s);
      //llvm::errs() << "adding upper " << u << "\n";
      qexp->addOrIncludes(u);
   }
   Query *q = new Query(qexp);
   Label *label = new Label(desc + "_label", q);
   Constraint *cscons = new Constraint(qexp->getOperand(), label);
   return cscons;
}

Constraint *InfoFlowEngine::generateRegExpCons(ETYPE type, std::string desc,
                                               std::vector<const char*> *orincludes, std::string tname="", 
                                               int field=-1, int arg=-1) {
   RegExpQueryExp *qexp = NULL;
   if (tname == "") {
      if (arg != -1)
         qexp = new RegExpQueryExp(desc, type, arg);
      else 
         qexp = new RegExpQueryExp(desc, type);
   }
   else if (field != -1)
      qexp = new RegExpQueryExp(desc, type, tname, field);
   else assert(0);
   for(int i=0; orincludes && i<orincludes->size(); i++) {
      std::string s((*orincludes)[i]);
      qexp->addOrIncludes(s);
      qexp->addOrIncludes(toLower(s));
      qexp->addOrIncludes(toUpper(s));
   }
   Query *q = new Query(qexp);
   Label *label = new Label(desc + "_label", q);
   Constraint *cscons = new Constraint(qexp->getOperand(), label);
   return cscons;
}

Constraint *InfoFlowEngine::generateRelationalCons(UTYPE type, const char *descp, Constraint *i1cons, Constraint *i2cons,
                                                  Constraint *san, int retval) {
   std::string desc(descp);
   RelationalQueryExp *qexp = new RelationalQueryExp(i1cons->getIdentifier(),i2cons->getIdentifier(),type);
   Query *q = new Query(qexp);
   q->addConstraint(i1cons);
   q->addConstraint(i2cons);
   if (san) 
      q->setSanitizationConstraint(san, retval);
   Label *label = new Label(desc, q);
   Constraint *cons = new Constraint(i1cons->getIdentifier(), label);
   return cons;
}

Constraint *InfoFlowEngine::generateRelationalCons(UTYPE type, std::string desc, Constraint *i1cons, Constraint *i2cons,
                                                  Constraint *san, int retval) {
   RelationalQueryExp *qexp = new RelationalQueryExp(i1cons->getIdentifier(),i2cons->getIdentifier(),type);
   Query *q = new Query(qexp);
   q->addConstraint(i1cons);
   q->addConstraint(i2cons);
   if (san)
      q->setSanitizationConstraint(san, retval);
   Label *label = new Label(desc, q);
   Constraint *cons = new Constraint(i1cons->getIdentifier(), label);
   return cons;
}

Constraint *InfoFlowEngine::generateTernaryConsVec(UTYPE type, const char *descp, Constraint *i1cons, Constraint *i2cons,
                                                Constraint *i3cons, Constraint *san, int retval, int retval2, int retval3) {
   std::string desc(descp);
   RelationalQueryExp *qexp = new RelationalQueryExp(i1cons->getIdentifier(),i2cons->getIdentifier(),
                                                     i3cons->getIdentifier(), type);
   Query *q = new Query(qexp);
   q->addConstraint(i1cons);
   q->addConstraint(i2cons);
   q->addConstraint(i3cons);
   if (san)
      q->setSanitizationConstraint(san, retval, retval2, retval3);
   Label *label = new Label(desc, q);
   Constraint *cons = new Constraint(i1cons->getIdentifier(), label);
   return cons;
}

Constraint *InfoFlowEngine::generateTernaryCons(UTYPE type, const char *descp, Constraint *i1cons, Constraint *i2cons,
                                                Constraint *i3cons, Constraint *san, int retval) {
   std::string desc(descp);
   RelationalQueryExp *qexp = new RelationalQueryExp(i1cons->getIdentifier(),i2cons->getIdentifier(), 
                                                     i3cons->getIdentifier(), type);
   Query *q = new Query(qexp);
   q->addConstraint(i1cons);
   q->addConstraint(i2cons);
   q->addConstraint(i3cons);
   if (san)
      q->setSanitizationConstraint(san, retval);
   Label *label = new Label(desc, q);
   Constraint *cons = new Constraint(i1cons->getIdentifier(), label);
   return cons;
}

Constraint *InfoFlowEngine::generateTernaryConsVec(UTYPE type, std::string desc, Constraint *i1cons, Constraint *i2cons,
                                 Constraint *i3cons, Constraint *san, int retval, int retval2, int retval3) {
   RelationalQueryExp *qexp = new RelationalQueryExp(i1cons->getIdentifier(),i2cons->getIdentifier(),
                                                     i3cons->getIdentifier(), type);
   Query *q = new Query(qexp);
   q->addConstraint(i1cons);
   q->addConstraint(i2cons);
   q->addConstraint(i3cons);
   if (san) {
      q->setSanitizationConstraint(san, retval, retval2, retval3);
   }
   Label *label = new Label(desc, q);
   Constraint *cons = new Constraint(i1cons->getIdentifier(), label);
   return cons;
}

Constraint *InfoFlowEngine::generateTernaryCons(UTYPE type, std::string desc, Constraint *i1cons, Constraint *i2cons,
                                                Constraint *i3cons, Constraint *san, int retval) {
   RelationalQueryExp *qexp = new RelationalQueryExp(i1cons->getIdentifier(),i2cons->getIdentifier(),
                                                     i3cons->getIdentifier(), type);
   Query *q = new Query(qexp);
   q->addConstraint(i1cons);
   q->addConstraint(i2cons);
   q->addConstraint(i3cons);
   if (san)
      q->setSanitizationConstraint(san, retval);
   Label *label = new Label(desc, q);
   Constraint *cons = new Constraint(i1cons->getIdentifier(), label);
   return cons;
}





void recordQuery(Constraint *cons) {
   Label *label = cons->getLabel();
   enableMap[label->getDescription()] = ((InfoFlowQueryLabel == "all" || InfoFlowQueryLabel == "" ||
                                          InfoFlowQueryLabel == label->getDescription()
                                        ) ? true : false);
   labelMap[label->getDescription()] = label;
}

Constraint *InfoFlowEngine::generateTaintsDataFieldCallsiteToPrimitiveCons(const char *tname, int field,
                                                                    const char *fname, const char *suffixp) {
   static int c = 0;
   c++;
   std::string suffix(suffixp);
   std::string str(fname);
   std::string tstr(tname);

   std::vector<const char*> fIncludes, tIncludes;
   fIncludes.push_back(fname);
   tIncludes.push_back(tname);

   std::string str1 = "type" + tstr + "field" + std::to_string(field) + std::to_string(c);
   Constraint *gtfcons = generateRegExpCons(fieldAddrExprT, str1, NULL, tname, field);

   std::string str2 = str + "arg" + std::to_string(c);
   Constraint *gfargqcons = generateRegExpCons(primitiveFuncArgT, str2, NULL);
   std::string str3 = str + "cs" + std::to_string(c);
   Constraint *gfcscons = generateRegExpCons(callsiteT, str3, &fIncludes);
   std::string str4 = str + "argpby" + std::to_string(c);
   Constraint *genfargcons = generateRelationalCons(actualparameterpassedby, str4,
                                                 gfargqcons, gfcscons);

   std::string str5 = tstr + "_" + str + "_" + suffix;
   Constraint *gdfcons = generateRelationalCons(taints, str5,
                                                gtfcons, genfargcons);
   recordQuery(gdfcons);
   return gdfcons;
}


Constraint *InfoFlowEngine::generateTaintsDataFieldCallsiteCons(const char *tname, int field, 
                                                                    const char *fname, const char *suffixp) {
   static int c = 0;
   c++;
   std::string suffix(suffixp);
   std::string str(fname);
   std::string tstr(tname);

   std::vector<const char*> fIncludes, tIncludes;
   fIncludes.push_back(fname);
   tIncludes.push_back(tname);

   std::string str1 = "type" + tstr + "field" + std::to_string(field) + std::to_string(c);
   Constraint *gtfcons = generateRegExpCons(fieldAddrExprT, str1, NULL, tname, field);

   std::string str2 = str + "arg" + std::to_string(c);
   Constraint *gfargqcons = generateRegExpCons(pointerFuncArgT, str2, NULL);
   std::string str3 = str + "cs" + std::to_string(c);
   Constraint *gfcscons = generateRegExpCons(callsiteT, str3, &fIncludes);
   std::string str4 = str + "argpby" + std::to_string(c);
   Constraint *genfargcons = generateRelationalCons(actualparameterpassedby, str4,
                                                 gfargqcons, gfcscons);

   std::string str5 = tstr + "_" + str + "_" + suffix;
   Constraint *gdfcons = generateRelationalCons(taints, str5,
                                                gtfcons, genfargcons);
   recordQuery(gdfcons);
   return gdfcons;
}

Constraint *InfoFlowEngine::generateTernaryForCallsiteCons(const char *fname, const char *suffixp) {
   static int c = 0;
   c++;
   std::string suffix(suffixp);
   std::string str(fname);
   std::vector<const char*> fIncludes;
   fIncludes.push_back(fname);
   std::string str2 = str + "arg" + std::to_string(c);   
   Constraint *gfargqcons = generateRegExpCons(pointerFuncArgT, str2, NULL);
   std::string str3 = str + "cs" + std::to_string(c);
   Constraint *gfcscons = generateRegExpCons(callsiteT, str3, &fIncludes);
   std::string str4 = str + "argpby" + std::to_string(c);
   Constraint *genfargcons = generateRelationalCons(actualparameterpassedby, str4,
                                                 gfargqcons, gfcscons);
   std::string str5 = str + suffix;
   Constraint *somegen = generateRegExpCons(genericValueT, "somegeneric", NULL); 
   Constraint *gdfcons = generateTernaryCons(taintsbothinseq, str5,
                                            somegen, genfargcons, genfargcons, NULL, 0);
   recordQuery(gdfcons);
   return gdfcons;
}

Constraint *InfoFlowEngine::generateTernaryForCallsiteWithSourceArgNoCons(const char *fname1, const char *fname2, 
                                                                          int argno, const char *suffixp) {
   static int c = 0;
   c++;
   std::string suffix(suffixp);
   std::string str(fname1);
   std::vector<const char*> fIncludes;
   fIncludes.push_back(fname1);
   std::string str2 = str + "arg" + std::to_string(c);
   Constraint *gfargqcons = generateRegExpCons(pointerFuncArgT, str2, NULL, "", -1, argno);
   std::string str3 = str + "cs" + std::to_string(c);
   Constraint *gfcscons = generateRegExpCons(callsiteT, str3, &fIncludes);
   std::string str4 = str + "argpby" + std::to_string(c);
   Constraint *genfargcons = generateRelationalCons(actualparameterpassedby, str4,
                                                 gfargqcons, gfcscons);

   std::string str_2(fname2);
   std::vector<const char*> fIncludes_2;
   fIncludes_2.push_back(fname2);
   std::string str2_2 = str_2 + "arg" + std::to_string(c);
   Constraint *gfargqcons_2 = generateRegExpCons(pointerFuncArgT, str2_2, NULL);
   std::string str3_2 = str_2 + "cs" + std::to_string(c);
   Constraint *gfcscons_2 = generateRegExpCons(callsiteT, str3_2, &fIncludes_2);
   std::string str4_2 = str_2 + "argpby" + std::to_string(c);
   Constraint *genfargcons_2 = generateRelationalCons(actualparameterpassedby, str4_2,
                                                 gfargqcons_2, gfcscons_2);

   std::string str5 = str + "_" + str_2 + "_" + suffix;
   Constraint *somegen = generateRegExpCons(genericValueT, "somegeneric", NULL);
   Constraint *gdfcons = generateTernaryCons(taintsbothinseq, str5,
                                            somegen, genfargcons, genfargcons_2, NULL, 0);
   recordQuery(gdfcons);
   return gdfcons;
}


Constraint *InfoFlowEngine::generateTernaryForCallsiteCons(const char *fname1, const char *fname2, const char *suffixp) {
   static int c = 0;
   c++;
   std::string suffix(suffixp);
   std::string str(fname1);
   std::vector<const char*> fIncludes;
   fIncludes.push_back(fname1);    
   std::string str2 = str + "arg" + std::to_string(c);
   Constraint *gfargqcons = generateRegExpCons(pointerFuncArgT, str2, NULL);
   std::string str3 = str + "cs" + std::to_string(c);
   Constraint *gfcscons = generateRegExpCons(callsiteT, str3, &fIncludes);
   std::string str4 = str + "argpby" + std::to_string(c);
   Constraint *genfargcons = generateRelationalCons(actualparameterpassedby, str4,
                                                 gfargqcons, gfcscons);

   std::string str_2(fname2);
   std::vector<const char*> fIncludes_2;
   fIncludes_2.push_back(fname2);
   std::string str2_2 = str_2 + "arg" + std::to_string(c);
   Constraint *gfargqcons_2 = generateRegExpCons(pointerFuncArgT, str2_2, NULL);
   std::string str3_2 = str_2 + "cs" + std::to_string(c);
   Constraint *gfcscons_2 = generateRegExpCons(callsiteT, str3_2, &fIncludes_2);
   std::string str4_2 = str_2 + "argpby" + std::to_string(c);
   Constraint *genfargcons_2 = generateRelationalCons(actualparameterpassedby, str4_2,
                                                 gfargqcons_2, gfcscons_2);

   std::string str5 = str + "_" + str_2 + "_" + suffix;
   Constraint *somegen = generateRegExpCons(genericValueT, "somegeneric", NULL);
   Constraint *gdfcons = generateTernaryCons(taintsbothinseq, str5,
                                            somegen, genfargcons, genfargcons_2, NULL, 0);
   recordQuery(gdfcons);
   return gdfcons;
}

Constraint *InfoFlowEngine::generateTernaryForCallsiteWithSourceTaintConsVec(const char *fname1, const char *fname2,
                                                           const char *suffixp, int retval, int retval2, int retval3) {
   static int c = 0;
   c++;
   std::string suffix(suffixp);
   std::string str(fname1);
   std::vector<const char*> fIncludes;
   fIncludes.push_back(fname1);
   std::string str2 = str + "arg" + std::to_string(c);
   Constraint *gfargqcons = generateRegExpCons(pointerFuncArgT, str2, NULL);
   std::string str3 = str + "cs" + std::to_string(c);
   Constraint *gfcscons = generateRegExpCons(callsiteT, str3, &fIncludes);
   std::string str4 = str + "argpby" + std::to_string(c);
   Constraint *genfargcons = generateRelationalCons(actualparameterpassedby, str4,
                                                 gfargqcons, gfcscons);

   std::string str_2(fname2);
   std::vector<const char*> fIncludes_2;
   fIncludes_2.push_back(fname2);
   std::string str2_2 = str_2 + "arg" + std::to_string(c);
   Constraint *gfargqcons_2 = generateRegExpCons(pointerFuncArgT, str2_2, NULL);
   std::string str3_2 = str_2 + "cs" + std::to_string(c);
   Constraint *gfcscons_2 = generateRegExpCons(callsiteT, str3_2, &fIncludes_2);
   std::string str4_2 = str_2 + "argpby" + std::to_string(c);
   Constraint *genfargcons_2 = generateRelationalCons(actualparameterpassedby, str4_2,
                                                 gfargqcons_2, gfcscons_2);


   std::string str5 = str + "_" + str_2 + "_" + suffix;
   Constraint *somegen = generateRegExpCons(genericValueT, "somegeneric", NULL);
   Constraint *gdfcons = generateTernaryConsVec(taintsbothinseq, str5,
                                                somegen, genfargcons, genfargcons_2, 
                                                genfargcons, retval, retval2, retval3);
   recordQuery(gdfcons);
   return gdfcons;
}

Constraint *InfoFlowEngine::generateTernaryForCallsiteWithSourceTaintCons(const char *fname1, const char *fname2, 
                                                                          const char *suffixp, int retval) {
   static int c = 0;
   c++;
   std::string suffix(suffixp);
   std::string str(fname1);
   std::vector<const char*> fIncludes;
   fIncludes.push_back(fname1);
   std::string str2 = str + "arg" + std::to_string(c);
   Constraint *gfargqcons = generateRegExpCons(pointerFuncArgT, str2, NULL);
   std::string str3 = str + "cs" + std::to_string(c);
   Constraint *gfcscons = generateRegExpCons(callsiteT, str3, &fIncludes);
   std::string str4 = str + "argpby" + std::to_string(c);
   Constraint *genfargcons = generateRelationalCons(actualparameterpassedby, str4,
                                                 gfargqcons, gfcscons);

   std::string str_2(fname2);
   std::vector<const char*> fIncludes_2;
   fIncludes_2.push_back(fname2);
   std::string str2_2 = str_2 + "arg" + std::to_string(c);
   Constraint *gfargqcons_2 = generateRegExpCons(pointerFuncArgT, str2_2, NULL);
   std::string str3_2 = str_2 + "cs" + std::to_string(c);
   Constraint *gfcscons_2 = generateRegExpCons(callsiteT, str3_2, &fIncludes_2);
   std::string str4_2 = str_2 + "argpby" + std::to_string(c);
   Constraint *genfargcons_2 = generateRelationalCons(actualparameterpassedby, str4_2,
                                                 gfargqcons_2, gfcscons_2);


   std::string str5 = str + "_" + str_2 + "_" + suffix;
   Constraint *somegen = generateRegExpCons(genericValueT, "somegeneric", NULL);
   Constraint *gdfcons = generateTernaryCons(taintsbothinseq, str5,
                                            somegen, genfargcons, genfargcons_2, genfargcons, retval);
   recordQuery(gdfcons);
   return gdfcons;
}

Constraint *InfoFlowEngine::generateTaintsPtrArgDirectCallToPtrArg(const char *sourcep, const char *sinkp,
                                                                        const char *suffixp, int retval=-1) {
   std::string source(sourcep);
   std::string sink(sinkp);
   std::string suffix(suffixp);

   std::vector<const char*> sourceIncludes;
   sourceIncludes.push_back(sourcep);

   Constraint *dcallcons =  generateRegExpCons(callsiteT, "sourcecs", &sourceIncludes);

   Constraint *argcons1 = generateRegExpCons(pointerFuncArgT, "srcarg", NULL, "", -1, -1);

   Constraint *srcargcons = generateRelationalCons(actualparameterpassedby, "argsrccs",
                                                            argcons1, dcallcons, NULL, 0);

   if (dcallcons) {
      Solution *sol = solve(dcallcons->getLabel());
      sol->print();
   }
      

   std::vector<const char*> sinkIncludes;
   sinkIncludes.push_back(sinkp);
   Constraint *sinkcscons =  generateRegExpCons(callsiteT, "sinkcs", &sinkIncludes);

   Constraint *argcons2 = generateRegExpCons(pointerFuncArgT, "sinkarg", NULL);

   Constraint *sinkargcons = generateRelationalCons(actualparameterpassedby, "argsinkcs",
                                                    argcons2, sinkcscons, NULL, 0);

   Constraint *cons = generateRelationalCons(taints, source + "_" + sink + "_" + suffix,
                                                             srcargcons, sinkargcons, srcargcons, retval);

   return cons;
}

Constraint *InfoFlowEngine::generateTaintsPtrArgDirectCallToPrimValArg(const char *sourcep, const char *sinkp,
                                                                        const char *suffixp, int argno=-1) {
   std::string source(sourcep);
   std::string sink(sinkp);
   std::string suffix(suffixp);                                                               

   std::vector<const char*> sourceIncludes;
   sourceIncludes.push_back(sourcep);

   Constraint *dcallcons =  generateRegExpCons(callsiteT, "sourcecs", &sourceIncludes);

   Constraint *argcons1 = generateRegExpCons(pointerFuncArgT, "srcarg", NULL, "", -1, argno);

   Constraint *srcargcons = generateRelationalCons(actualparameterpassedby, "argsrccs",
                                                            argcons1, dcallcons, NULL, 0);

   std::vector<const char*> sinkIncludes;
   sinkIncludes.push_back(sinkp);
   Constraint *sinkcscons =  generateRegExpCons(callsiteT, "sinkcs", &sinkIncludes);

   Constraint *argcons2 = generateRegExpCons(primitiveFuncArgT, "sinkarg", NULL);

   Constraint *sinkargcons = generateRelationalCons(actualparameterpassedby, "argsinkcs",
                                                    argcons2, sinkcscons, NULL, 0);

   Constraint *valuecons = generateRegExpCons(genericValueT, "value", NULL);

   Constraint *valuepbsrcargcons = generateRelationalCons(pointedby, "pbsrcarg",
                                                                  valuecons, srcargcons);
   Constraint *recvtaintsmemcpycons = generateRelationalCons(taints, source + "_" + sink + "_" + suffix,
                                                             valuepbsrcargcons, sinkargcons, NULL, 0);

   return recvtaintsmemcpycons;
}

// a pointer arg of indirect call of some function pointer field in datastname taints a nonpointer arg of sink
// e.g., a receive callback arg, e.g., buffer filled by an attacker, taints the length arg of memcpy
Constraint *InfoFlowEngine::generateTaintsPtrArgIndirectCallToPrimValArg(const char *datastnamep, const char *sinkp, 
                                                                         const char *suffixp) {

   std::string datastname(datastnamep);
   std::string sink(sinkp);
   std::string suffix(suffixp);

   std::vector<const char*> datastIncludes;
   datastIncludes.push_back(datastnamep);

   Constraint *cbcons = generateRegExpCons(funcptrfieldT, "cb", NULL);

   Constraint *stcons = generateRegExpCons(structT, "datast", &datastIncludes);

   Constraint *cbdefinstcons = generateRelationalCons(callbackdefinedin, "cbdefin",
                                                       cbcons, stcons, NULL, 0);

   Constraint *indssrccscons =  generateRegExpCons(callsiteT, "sourcecs", NULL);

   Constraint *indcbcons = generateRelationalCons(indirectcallof, "indcb", indssrccscons, cbdefinstcons, NULL, 0);
   

   Constraint *argcons1 = generateRegExpCons(pointerFuncArgT, "srcarg", NULL);

   Constraint *srcargcons = generateRelationalCons(actualparameterpassedby, "argsrccs",
                                                            argcons1, indcbcons, NULL, 0);

   Constraint *argcons2 = generateRegExpCons(primitiveFuncArgT, "sinkarg", NULL);

   std::vector<const char*> sinkIncludes;
   sinkIncludes.push_back(sinkp);
   Constraint *sinkcscons =  generateRegExpCons(callsiteT, "sinkcs", &sinkIncludes);

   Constraint *sinkargcons = generateRelationalCons(actualparameterpassedby, "argsinkcs",
                                                    argcons2, sinkcscons, NULL, 0);

   Constraint *valuecons = generateRegExpCons(genericValueT, "value", NULL);

   Constraint *valuepbsrcargcons = generateRelationalCons(pointedby, "pbsrcarg",
                                                                  valuecons, srcargcons);
   Constraint *recvtaintsmemcpycons = generateRelationalCons(taints, datastname + "_" + sink + "_" + suffix,
                                                             valuepbsrcargcons, sinkargcons, NULL, 0);   

   return recvtaintsmemcpycons;
}

bool InfoFlowEngine::handleQuery() {

  if (InfoFlowQueryType != "") {
     llvm::errs() << "Query type: " << InfoFlowQueryType << "\n";   
     if (InfoFlowQueryType == "DataStr") { 
        assert(InfoFlowQueryOps.size() >= 1 && "Need 1 operand for DataStr\n"); 
        std::vector<const char*> datastIncludes;
        datastIncludes.push_back(InfoFlowQueryOps[0].c_str());
        Constraint *cons = generateRegExpCons(structT, "datast", &datastIncludes);
        Solution *sol = solve(cons->getLabel());
        if (sol)
           sol->print();
        return true;
     }
     else if (InfoFlowQueryType == "CallbackInDataStr") {
        assert(InfoFlowQueryOps.size() >= 1 && "Need 1 operand for DataStr\n"); 
        std::vector<const char*> datastIncludes;
        datastIncludes.push_back(InfoFlowQueryOps[0].c_str());
        Constraint *stcons = generateRegExpCons(structT, "datast", &datastIncludes);
        Constraint *cbcons = generateRegExpCons(funcptrfieldT, "cb", NULL);
        Constraint *cons = generateRelationalCons(callbackdefinedin, "cbdefin",
                                                       cbcons, stcons, NULL, 0);
        Solution *sol = solve(cons->getLabel());
        if (sol)
           sol->print();
        return true;
     }
     else if (InfoFlowQueryType == "TaintsPtrArgDirectCallToPrimValArg") {
         assert(InfoFlowQueryOps.size() >= 3 && "Need 3 operands forTaintsPtrArgDirectCallToPrimValArg\n");
         Constraint *cons = generateTaintsPtrArgDirectCallToPrimValArg(InfoFlowQueryOps[0].c_str(),
                                             InfoFlowQueryOps[1].c_str(), InfoFlowQueryOps[2].c_str());
         stats << "Query: TaintsPtrArgDirectCallToPrimValArg, source=" << InfoFlowQueryOps[0] 
               << " sink=" << InfoFlowQueryOps[1] << " desc=" << InfoFlowQueryOps[2] << "\n";
         Solution *sol = solve(cons->getLabel());
         if (sol)
           sol->print();
        return true;
     }
     else if (InfoFlowQueryType == "TaintsDirectCallPtrArgToPtrArgWithSan") {
        assert(InfoFlowQueryOps.size() >= 4 && "Need 4 operands for TaintsDirectCallPtrArgToPtrArgWithSan\n");
        Constraint *cons = generateTaintsPtrArgDirectCallToPtrArg(InfoFlowQueryOps[0].c_str(),
                               InfoFlowQueryOps[1].c_str(), InfoFlowQueryOps[2].c_str(), std::stoi(InfoFlowQueryOps[3]));
        stats << "Query: TaintsDirectCallPtrArgToPtrArgWithSan, source=" <<  InfoFlowQueryOps[0]
              << " sink=" << InfoFlowQueryOps[1] << " desc=" <<  InfoFlowQueryOps[2]
              << " sanitizeOnReturn=" << InfoFlowQueryOps[3] << "\n";
        solve(cons->getLabel());
        return true;
     }
     else if (InfoFlowQueryType == "TaintsPtrArgNoDirectCallToPrimValArg") {
         assert(InfoFlowQueryOps.size() >= 4 && "Need 4 operands forTaintsPtrArgDirectCallToPrimValArg\n");
         Constraint *cons = generateTaintsPtrArgDirectCallToPrimValArg(InfoFlowQueryOps[0].c_str(),
                                             InfoFlowQueryOps[2].c_str(),
                                             InfoFlowQueryOps[3].c_str(), std::stoi(InfoFlowQueryOps[1]));
         stats << "Query: TaintsPtrArgNoDirectCallToPrimValArg, source=" << InfoFlowQueryOps[0]
               << " argno=" << std::stoi(InfoFlowQueryOps[1]) << " sink=" << InfoFlowQueryOps[2] 
               << " desc=" << InfoFlowQueryOps[3] << "\n";
         Solution *sol = solve(cons->getLabel());
         if (sol)
           sol->print();
        return true;
     }
     else if (InfoFlowQueryType == "TaintsPtrArgIndirectCallToPrimValArg") {
        assert(InfoFlowQueryOps.size() >= 3 && "Need 3 operands for TaintsPtrArgIndirectCallToPrimValArg\n");
        Constraint *cons = generateTaintsPtrArgIndirectCallToPrimValArg(InfoFlowQueryOps[0].c_str(),
                                             InfoFlowQueryOps[1].c_str(), InfoFlowQueryOps[2].c_str());
        stats << "Query: TaintsPtrArgIndirectCallToPrimValArg, data type=" << InfoFlowQueryOps[0] 
              << " sink=" << InfoFlowQueryOps[1] << " desc=" << InfoFlowQueryOps[2] << "\n";
        solve(cons->getLabel());
        return true;
     }
     else if (InfoFlowQueryType == "TaintsDataFieldCallsitePrimitveArg") {
        llvm::errs() << "Ops: " << InfoFlowQueryOps[0] << "," << InfoFlowQueryOps[1] << ","
                                << InfoFlowQueryOps[2] << "," << InfoFlowQueryOps[3] << "\n";
        assert(InfoFlowQueryOps.size() >= 4 && "Need 4 operands for TaintsDataFieldCallsitePrimitveArg\n");
        Constraint *cons = generateTaintsDataFieldCallsiteToPrimitiveCons(InfoFlowQueryOps[0].c_str(), 
                                std::stoi(InfoFlowQueryOps[1]),
                                            InfoFlowQueryOps[2].c_str(), InfoFlowQueryOps[3].c_str());
        stats << "Query: TaintsDataFieldCallsitePrimitveArg, data type= " <<  InfoFlowQueryOps[0] 
              << " field=" << InfoFlowQueryOps[1]
              << " sink=" << InfoFlowQueryOps[2] << " desc=" << InfoFlowQueryOps[3] << "\n";
        solve(cons->getLabel());
        return true;
     }
     else if (InfoFlowQueryType == "TaintsDataFieldCallsite") {
        llvm::errs() << "Ops: " << InfoFlowQueryOps[0] << "," << InfoFlowQueryOps[1] << ","
                                << InfoFlowQueryOps[2] << "," << InfoFlowQueryOps[3] << "\n";
        assert(InfoFlowQueryOps.size() >= 4 && "Need 4 operands for TaintsDataFieldCallsite\n");
        Constraint *cons = generateTaintsDataFieldCallsiteCons(InfoFlowQueryOps[0].c_str(), std::stoi(InfoFlowQueryOps[1]),
                                            InfoFlowQueryOps[2].c_str(), InfoFlowQueryOps[3].c_str());     
        stats << "Query: TaintsDataFieldCallsite, data type= " <<  InfoFlowQueryOps[0] << " field=" << InfoFlowQueryOps[1] 
              << " sink=" << InfoFlowQueryOps[2] << " desc=" << InfoFlowQueryOps[3] << "\n";   
        solve(cons->getLabel());           
        return true;
     }
     else if (InfoFlowQueryType == "TernaryForCallsiteWithSourceTaint") {
        assert(InfoFlowQueryOps.size() >= 4 && "Need 4 operands for TernaryForCallsiteWithSourceTaint\n");
        Constraint *cons = generateTernaryForCallsiteWithSourceTaintCons(InfoFlowQueryOps[0].c_str(), 
                               InfoFlowQueryOps[1].c_str(), InfoFlowQueryOps[2].c_str(), std::stoi(InfoFlowQueryOps[3]));
        stats << "Query: TernaryForCallsiteWithSourceTaint, source=" <<  InfoFlowQueryOps[0] 
              << " sink=" << InfoFlowQueryOps[1] << " desc=" <<  InfoFlowQueryOps[2] 
              << " sanitizeOnReturn=" << InfoFlowQueryOps[3] << "\n";
        solve(cons->getLabel());
        return true;
     }
     else if (InfoFlowQueryType == "TernaryForCallsiteWithSourceTaintVec") {
        assert(InfoFlowQueryOps.size() >= 3 && "Need 3 operands for TernaryForCallsiteWithSourceTaintVec\n");
        //assert(InfoFlowQuerySanVec.size() >= 3 && "Need 3 operands for TernaryForCallsiteWithSourceTaintVec\n");
        assert(InfoFlowQuerySanVecFile != "" && "Should provide sanitization values in a file\n");
        std::vector<int> svals;
        readNums(InfoFlowQuerySanVecFile.c_str(), svals);
        assert(svals.size() == 3);
        Constraint *cons = generateTernaryForCallsiteWithSourceTaintConsVec(InfoFlowQueryOps[0].c_str(),
                               InfoFlowQueryOps[1].c_str(), InfoFlowQueryOps[2].c_str(), 
                               svals[0], svals[1], svals[2]);
                               //InfoFlowQuerySanVec[0], InfoFlowQuerySanVec[1], InfoFlowQuerySanVec[2]); 
                               //std::stoi(InfoFlowQueryOps[3]), std::stoi(InfoFlowQueryOps[4]), 
                               //std::stoi(InfoFlowQueryOps[5]));
        stats << "Query: TernaryForCallsiteWithSourceTaintVec, source=" <<  InfoFlowQueryOps[0]
              << " sink=" << InfoFlowQueryOps[1] << " desc=" <<  InfoFlowQueryOps[2]
              << " sanitizeOnReturn=" << svals[0] << " " << svals[1] << " " << svals[2] << "\n";
              //<< InfoFlowQueryOps[3] << "," << InfoFlowQueryOps[4] 
              //<< "," << InfoFlowQueryOps[5] << "\n";
        solve(cons->getLabel());
        return true;
     }
     else if (InfoFlowQueryType == "TernaryForTwoCallsite") {
        assert(InfoFlowQueryOps.size() >= 3 && "Need 3 operands for TernaryForCallsiteWithSourceTaint\n");   
        Constraint *cons = generateTernaryForCallsiteCons(InfoFlowQueryOps[0].c_str(),
                               InfoFlowQueryOps[1].c_str(), InfoFlowQueryOps[2].c_str());
        stats << "Query: TernaryForTwoCallsite, source=" << InfoFlowQueryOps[0] 
              << " sink=" << InfoFlowQueryOps[1] << " desc=" << InfoFlowQueryOps[2] << "\n"; 
        solve(cons->getLabel());
        return true;
     }
     else if (InfoFlowQueryType == "TernaryForTwoCallsiteWithSourceArgno") {
        assert(InfoFlowQueryOps.size() >= 4 && "Need 4 operands for TernaryForTwoCallsiteWithSourceArgno\n");
        Constraint *cons = generateTernaryForCallsiteWithSourceArgNoCons(InfoFlowQueryOps[0].c_str(),
                               InfoFlowQueryOps[1].c_str(), std::stoi(InfoFlowQueryOps[2]), InfoFlowQueryOps[3].c_str());
        stats << "Query: TernaryForTwoCallsiteWithSourceArgNo, source=" << InfoFlowQueryOps[0]
              << " sink=" << InfoFlowQueryOps[1] << " arg=" << std::stoi(InfoFlowQueryOps[2]) 
              << " desc=" << InfoFlowQueryOps[3] << "\n";
        solve(cons->getLabel());
        return true;
     }
     else if (InfoFlowQueryType == "TernaryForCallsite") {
        assert(InfoFlowQueryOps.size() >= 2 && "Need 2 operands for TernaryForCallsiteWithSourceTaint\n");   
        Constraint *cons = generateTernaryForCallsiteCons(InfoFlowQueryOps[0].c_str(),
                                                          InfoFlowQueryOps[1].c_str());
        stats << "Query: TernaryForCallsite, source=" << InfoFlowQueryOps[0] 
              << " sink=" << InfoFlowQueryOps[0] << " desc=" << InfoFlowQueryOps[1] << "\n";
        Solution *sol = solve(cons->getLabel());
        if (sol)
           sol->print();
        return true;
     }
  }
  return false;
}

void InfoFlowEngine::runQuery() {

   time_t beforeSum, afterSum;
   time(&beforeSum); 
   llvm::errs() << "Summarizing SVFG\n";
   stats << "Summarizing SVFG\n";
   summarizeSVFG();
   time(&afterSum);
   stats << "Sumarization time=" << difftime(afterSum, beforeSum) << "\n"; 
   llvm::errs() << "Sumarization time=" << difftime(afterSum, beforeSum) << "\n";
   stats.flush(); 
   //exit(0);

   if (handleQuery()) {
      return;
   }

   Constraint *valuecons = generateRegExpCons(genericValueT, "value", NULL);

   Constraint *racons = generateRegExpCons(pointerFuncArgT, "recvarg", NULL);

   Constraint *dirracons = generateRegExpCons(pointerFuncArgT, "dirrecvarg", NULL);

   Constraint *sanacons = generateRegExpCons(funcArgT, "sanarg", NULL);

   Constraint *recvcscons =  generateRegExpCons(callsiteT, "recvcs", NULL);;

   std::vector<const char*> recvIncludes;
   recvIncludes.push_back("read");
   recvIncludes.push_back("Read");
   recvIncludes.push_back("READ");
   recvIncludes.push_back("receive");
   recvIncludes.push_back("Recv");
   recvIncludes.push_back("Receive");
   recvIncludes.push_back("RECV");
   recvIncludes.push_back("RECEIVE");
   Constraint *dirrecvcscons = generateRegExpCons(callsiteT, "dirreccs", &recvIncludes);

   std::vector<const char*> recvsanIncludes;
   recvsanIncludes.push_back("iotc_data_desc_will_it_fit"); // success return value is 1  !!! need to specify that way
   recvsanIncludes.push_back("iotc_data_desc_assure_buf_len");
   Constraint *sancscons = generateRegExpCons(callsiteT, "sanrecv", &recvsanIncludes);

   Constraint *recvcbcons = generateRegExpCons(funcptrfieldT, "recvcb", NULL);

   std::vector<const char*> netstIncludes;
   netstIncludes.push_back("Network");
   netstIncludes.push_back("mbedtls_ssl_context");
   netstIncludes.push_back("mbedtls_tls_context");
   Constraint *networkstcons = generateRegExpCons(structT, "networkstruct", &netstIncludes);

   Constraint *recvdefnetcons = generateRelationalCons(callbackdefinedin, "recvinnetworkLabel", 
                                                       recvcbcons, networkstcons, NULL, 0);

   // recvcallsite indirectcallof recvcallback
   Constraint *recvindrecvcbcons = generateRelationalCons(indirectcallof, "recvindrecvcb", recvcscons, recvdefnetcons, NULL, 0);

   // recvarg actualparameterpassedby recvcallsite
   Constraint *recvargpbrecvcscons = generateRelationalCons(actualparameterpassedby, "recvargpbrecvcs", 
                                                            racons, recvindrecvcbcons, NULL, 0);

   // dirrecvarg actualparameterpassedby dirrecvcallsite
   Constraint *dirrecvargpbrecvcscons = generateRelationalCons(actualparameterpassedby, "dirrecvargpbrecvcs", 
                                                               dirracons, dirrecvcscons, NULL, 0);

   // sanitizetarg actualparameterpassedby sanitizercallsite
   Constraint *sanargpbsanscons = generateRelationalCons(actualparameterpassedby, "sanargpbsancs",
                                                         sanacons, sancscons, NULL, 0);


   // memcpyarg
   Constraint *macons = generateRegExpCons(primitiveFuncArgT, "memcpyarg", NULL);

   // memcpy callsites
   std::vector<const char*> memcpycsIncludes;
   memcpycsIncludes.push_back("memcpy");
   memcpycsIncludes.push_back("memcmp");
   memcpycsIncludes.push_back("strcpy");
   memcpycsIncludes.push_back("strncpy");
   Constraint *mcscons = generateRegExpCons(callsiteT, "memcopycs", &memcpycsIncludes);

   // memcpyarg actualparameterpassedby  memcpycallsite
   Constraint *margmcscons = generateRelationalCons(actualparameterpassedby, "argpbmemcpycs", 
                                                    macons, mcscons, NULL, 0);

   // (value taints memcpyarg)
   //      where
   //         value pointed_by recvarg  
   //            where
   //                value is a genericValueT
   //                recvarg is a pointerFuncArgT
   //                  where 
   //                    recvarg actualparameterpassedby recvcallsite
   //                       where 
   //                         recvcallsite is a callsiteT
   //                            where 
   //                               recvcallsite indirectcallof recvcallback
   //                                  where 
   //                                     recvcallback is a funcptrfieldT
   //                                         where
   //                                             recvcallback callbackdefinedin networkStruct
   //                                                where 
   //                                                   networkStruct is a structT
   //                                                      where
   //                                                          networkStruct includes "Network"
   //         memcpyarg is a pointerFuncArgT
   //            where
   //                memcpyarg actualparameterpassedby  memcpycallsite
   //                    where memcpycallsite is a callsiteT
   //                         where
   //                             memcpycallsite equals memcpy

   // The buffer updated by receive flows into memcpy as a source or dest buffer

   // value pointedby recvarg
   Constraint *valuepointedbyrecvargcons = generateRelationalCons(pointedby, "valuepointedbyrecvarg",
                                                                  valuecons, recvargpbrecvcscons);



   // value pointedby recvarg taints memcpy
   Constraint *recvtaintsmemcpycons = generateRelationalCons(taints, "vpbyrecttaintsmemcpycs", 
                                                             valuepointedbyrecvargcons, margmcscons, NULL, 0);
   recordQuery(recvtaintsmemcpycons);

   //  value pointedby recvarg taints memcpy with sanitizer
   Constraint *sanrecvtaintsmemcpycons = generateRelationalCons(taints, "sanvpbyrecttaintsmemcpycs",
                                                                valuepointedbyrecvargcons, margmcscons, sanargpbsanscons, 0);
   recordQuery(sanrecvtaintsmemcpycons);
   Constraint *valuepointedbydirrecvargcons = generateRelationalCons(pointedby, "vpbydirrecvarg",
                                                                     valuecons, dirrecvargpbrecvcscons, NULL, 0);

   Constraint *vpbydirrecvtaintsmemcpyqcons = generateRelationalCons(taints, "vpbydirrecvargtaintsmemcpy", 
                                                                     valuepointedbydirrecvargcons, margmcscons, NULL, 0);
   recordQuery(vpbydirrecvtaintsmemcpyqcons);

   Constraint *sandirrecvtaintsmemcpy = generateRelationalCons(taints,"sandirrecvtaintsmemcpy",
                                                                valuepointedbydirrecvargcons, margmcscons, sanargpbsanscons, 0);
   recordQuery(sandirrecvtaintsmemcpy);

   // handshakearg
   Constraint *handshakeacons = generateRegExpCons(pointerFuncArgT, "handshakearg", NULL);

   // handshakecs
   std::vector<const char*> handshakecsIncludes;
   handshakecsIncludes.push_back("mbedtls_ssl_handshake");
   Constraint *handshakecscons = generateRegExpCons(callsiteT, "handshakecs", &handshakecsIncludes);

   // handshakearg actualparameterpassedby  handshakecallsite
   Constraint *hsapyhscscons = generateRelationalCons(actualparameterpassedby, "hsapyhscsLabel", 
                                                      handshakeacons, handshakecscons, NULL, 0);
   recordQuery(hsapyhscscons);

   // tlsrwarg
   Constraint *tlsrwargcons = generateRegExpCons(pointerFuncArgT, "tlsrwarg", NULL);

   // tlsrwcs
   std::vector<const char*> tlsrwcsIncludes;
   tlsrwcsIncludes.push_back("mbedtls_ssl_read");   
   Constraint *tlsrwcscons =  generateRegExpCons(callsiteT, "tlsrwcs", &tlsrwcsIncludes);

   // tlsarg actualparameterpassedby tlsrwcs
   Constraint *tlsrwargpbttlscscons = generateRelationalCons(actualparameterpassedby, "tlsrwargpbytlsrwcs",
                                                      tlsrwargcons, tlsrwcscons, NULL, 0);
   recordQuery(tlsrwargpbttlscscons);

   // somearg
   Constraint *someargcons = generateRegExpCons(pointerFuncArgT, "somearg", NULL);

   // IoT query1
   // somearg taintsbothinseq handshakearg, tlsrwarg sanitizedby handshakecs on return 0
   Constraint *taintsbothinseqcons = generateTernaryCons(taintsbothinseq, "mbedtlshandshakerwcorrupted",
                                                    someargcons, hsapyhscscons, tlsrwargpbttlscscons, hsapyhscscons, 0);
   recordQuery(taintsbothinseqcons);

   // freecs
   std::vector<const char*> freeIncludes;
   freeIncludes.push_back("free");
   freeIncludes.push_back("Free");
   freeIncludes.push_back("FREE");

   Constraint *freecscons = generateRegExpCons(callsiteT, "freecs", &freeIncludes);  
  
   // some generic value
   Constraint *somegen = generateRegExpCons(genericValueT, "somegeneric", NULL);

   // double-free
   Constraint *doublefreecons = generateTernaryCons(taintsbothinseq, "doublefree",
                                                    somegen, freecscons, freecscons, NULL, 0);
   recordQuery(doublefreecons);

   // opencs
   std::vector<const char*> openIncludes;
   openIncludes.push_back("open");
   openIncludes.push_back("Open");
   openIncludes.push_back("OPEN");
   Constraint *opencscons = generateRegExpCons(callsiteT, "opencs", &openIncludes);
  
   // openining twice 
   // to do: opening twice without a close in between 
   // it will require taints all in sequence operator with  four operands (three on the right handside)
   Constraint *doubleopencons = generateTernaryCons(taintsbothinseq, "doubleopen", 
                                                    somegen, opencscons, opencscons, NULL, 0);
   recordQuery(doubleopencons); 

   // (Google specific) IoT query2
   std::vector<const char*> q1Includes;
   q1Includes.push_back("iotc_bsp_tls_recv_callback");
   Constraint *csq1cons = generateRegExpCons(callsiteT, "q1cs", &q1Includes);
   Constraint *argq1cons = generateRegExpCons(pointerFuncArgT, "q1arg", NULL);
   Constraint *iotrecvargcons = generateRelationalCons(actualparameterpassedby, "iotrecvarg",
                                                       argq1cons, csq1cons); 
   Constraint *vpbyiotrecvcbargcons = generateRelationalCons(pointedby, "vpbyiotrecvcbarg",
                                                             valuecons, iotrecvargcons);
   Constraint *query1cons = generateRelationalCons(taints, "iotrecvcbargtaintsmemcpy",
                                                   vpbyiotrecvcbargcons, margmcscons, NULL, 0); 
   recordQuery(query1cons);

   // (google specific) IoT query3
   std::vector<const char*> gf1Includes;
   gf1Includes.push_back("iotc_memory_limiter_free");
   Constraint *gf1cscons = generateRegExpCons(callsiteT, "fcs", &gf1Includes);
   Constraint *fargcons = generateRelationalCons(actualparameterpassedby, "freearg",
                                                 argq1cons, gf1cscons);
   Constraint *dfcons = generateTernaryCons(taintsbothinseq, "gdoublefree1",
                                            somegen, fargcons, fargcons, NULL, 0);
   recordQuery(dfcons);

   // IoT query4
   std::vector<const char*> gopenIncludes;
   gopenIncludes.push_back("open");
   Constraint *gopencscons = generateRegExpCons(callsiteT, "opencs", &gopenIncludes);
   Constraint *oargcons = generateRelationalCons(actualparameterpassedby, "openarg",
                                                 argq1cons, gopencscons);
   Constraint *docons = generateTernaryCons(taintsbothinseq, "gdoubleopen",
                                            somegen, oargcons, oargcons, NULL, 0);
   recordQuery(docons);
    

   // IoT query5
   std::vector<const char*> decryptIncludes;
   decryptIncludes.push_back("decrypt");   
   decryptIncludes.push_back("Decrypt");   
   decryptIncludes.push_back("DECRYPT");   
   Constraint *decryptscs = generateRegExpCons(callsiteT, "decryptscs", &decryptIncludes);
   Constraint *dargcons = generateRegExpCons(pointerFuncArgT, "darg", NULL);
   Constraint *decryptargcons =  generateRelationalCons(actualparameterpassedby, "decryptarg",
                                                        dargcons, decryptscs);
   std::vector<const char*> sendIncludes;
   sendIncludes.push_back("send");
   sendIncludes.push_back("Send");
   sendIncludes.push_back("SEND");
   sendIncludes.push_back("write");
   sendIncludes.push_back("Write");
   sendIncludes.push_back("WRITE");
   sendIncludes.push_back("print");
   sendIncludes.push_back("Print");
   sendIncludes.push_back("PRINT");
   Constraint *sendcs = generateRegExpCons(callsiteT, "sendcs", &sendIncludes);
   Constraint *sargcons = generateRegExpCons(pointerFuncArgT, "sarg", NULL);
   Constraint *sendargcons = generateRelationalCons(actualparameterpassedby, "sendarg",
                                                    sargcons, sendcs);
   Constraint *decrypttaintsendcons = generateTernaryCons(taintsbothinseq, "decrypttaintsend", 
                                                     somegen, decryptargcons, sendargcons, NULL, 0);
   recordQuery(decrypttaintsendcons);

   // IoT query6
   Constraint *df1cons = generateTernaryForCallsiteCons("_iot_security_be_software_buffer_free", "_doublefree");
   Constraint *df2cons = generateTernaryForCallsiteCons("iot_api_onboarding_config_mem_free", "_doublefree");
   Constraint *df3cons = generateTernaryForCallsiteCons("iot_api_device_info_mem_free", "_doublefree");
   Constraint *df4cons = generateTernaryForCallsiteCons("iot_api_prov_data_mem_free", "_doublefree");
   Constraint *df5cons = generateTernaryForCallsiteCons("iot_os_free", "_doublefree");

   // IoT query7
   // (SmartThings SDK specific)
   Constraint *nvleak1 = generateTernaryForCallsiteCons("_iot_nv_read_data", "write", "nvleak");
   Constraint *nvleak2 = generateTernaryForCallsiteCons("_iot_nv_read_data", "dump", "nvleak");
   Constraint *nvleak3 = generateTernaryForCallsiteCons("_iot_nv_read_data", "send", "nvleak");
   Constraint *nvleak4 = generateTernaryForCallsiteCons("_iot_nv_read_data", "print", "nvleak");
   Constraint *nvleak5 = generateTernaryForCallsiteCons("_iot_nv_read_data", "iot_bsp_debug", "nvleak");
   Constraint *nvleak6 = generateTernaryForCallsiteCons("_iot_nv_read_data", "memset", "nvleak"); 
   Constraint *nvleak7 = generateTernaryForCallsiteCons("iot_nv_get_prov_data", "memset", "nvleak");
   // are there any clearing even if the return is successfull?
   Constraint *nvleak8 = generateTernaryForCallsiteWithSourceTaintCons("iot_nv_get_prov_data", "memset", "nvleak_ret1", 1);
   // are there any clearing even if the return is not successful?
   Constraint *nvleak9 = generateTernaryForCallsiteWithSourceTaintCons("iot_nv_get_prov_data", "memset", "nvleak_ret0", 0);

   Constraint *keyleak1 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_ed25519_key", "write", "keyleak");
   Constraint *keyleak2 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_rsa", "write", "keyleak");
   Constraint *keyleak3 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_ecdsa", "write", "keyleak");
   Constraint *keyleak4 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_key", "write", "keyleak");
   Constraint *keyleak5 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_ed25519_key", "dump", "keyleak");
   Constraint *keyleak6 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_rsa", "dump", "keyleak");
   Constraint *keyleak7 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_ecdsa", "dump", "keyleak");
   Constraint *keyleak8 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_key", "dump", "keyleak");
   Constraint *keyleak9 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_ed25519_key", "send", "keyleak");
   Constraint *keyleak10 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_rsa", "send", "keyleak");
   Constraint *keyleak11 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_ecdsa", "send", "keyleak");
   Constraint *keyleak12 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_key", "send", "keyleak");
   Constraint *keyleak13 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_ed25519_key", "print", "keyleak");
   Constraint *keyleak14 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_rsa", "print", "keyleak");
   Constraint *keyleak15 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_ecdsa", "print", "keyleak");
   Constraint *keyleak16 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_key", "print", "keyleak");
   Constraint *keyleak17 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_ed25519_key", "iot_bsp_debug", "keyleak");
   Constraint *keyleak18 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_rsa", "iot_bsp_debug", "keyleak");
   Constraint *keyleak19 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_ecdsa", "iot_bsp_debug", "keyleak");
   Constraint *keyleak20 = generateTernaryForCallsiteCons("_iot_security_be_software_pk_load_key", "iot_bsp_debug", "keyleak");
   

   Constraint *wifileak1 = generateTernaryForCallsiteCons("iot_nv_set_wifi_prov_data", "iot_bsp_debug", "wifileak");
   //"esp_wifi_get_config",
   Constraint *wifileak2 = generateTernaryForCallsiteCons("iot_bsp_wifi_set_mode", "iot_bsp_debug", "wifileak");

   Constraint *sharedsecretleak = generateTernaryForCallsiteCons("iot_security_ecdh_compute_shared_secret", 
                                                                 "encrypt", "secretleak");

   // does wifi password leak to ...
   Constraint *wifipcons1 = generateTaintsDataFieldCallsiteCons("iot_wifi_prov_data", 1, "iot_bsp_debug", "wpleak");
   Constraint *wifipcons1_1 = generateTaintsDataFieldCallsiteCons("iot_wifi_prov_data", 1, "write", "wpleak");
   Constraint *wifipcons1_2 = generateTaintsDataFieldCallsiteCons("iot_wifi_prov_data", 1, "send", "wpleak");
   Constraint *wifipcons1_3 = generateTaintsDataFieldCallsiteCons("iot_wifi_prov_data", 1, "print", "wpleak");
   Constraint *wifipcons1_4 = generateTaintsDataFieldCallsiteCons("iot_wifi_prov_data", 1, "dump", "wpleak");

   Constraint *wifipcons2 = generateTaintsDataFieldCallsiteCons("iot_wifi_conf", 2, "iot_bsp_debug", "wpleak");
   Constraint *wifipcons2_1 = generateTaintsDataFieldCallsiteCons("iot_wifi_conf", 2, "write", "wpleak");
   Constraint *wifipcons2_2 = generateTaintsDataFieldCallsiteCons("iot_wifi_conf", 2, "send", "wpleak");
   Constraint *wifipcons2_3 = generateTaintsDataFieldCallsiteCons("iot_wifi_conf", 2, "print", "wpleak");
   Constraint *wifipcons2_4 = generateTaintsDataFieldCallsiteCons("iot_wifi_conf", 2, "dump", "wpleak");

   // does private key leak to ...
   // seckey field
   Constraint *privkleak1 = generateTaintsDataFieldCallsiteCons("iot_security_pk_params", 2, "iot_bsp_debug", "privleak");
   Constraint *privkleak1_1 = generateTaintsDataFieldCallsiteCons("iot_security_pk_params", 2, "write", "privleak");
   Constraint *privkleak1_2 = generateTaintsDataFieldCallsiteCons("iot_security_pk_params", 2, "send", "privleak");
   Constraint *privkleak1_3 = generateTaintsDataFieldCallsiteCons("iot_security_pk_params", 2, "print", "privleak");
   Constraint *privkleak1_4 = generateTaintsDataFieldCallsiteCons("iot_security_pk_params", 2, "dump", "privleak");
   // key field
   Constraint *privkleak2 = generateTaintsDataFieldCallsiteCons("iot_security_cipher_params", 1, "iot_bsp_debug", "privleak");
   Constraint *privkleak2_1 = generateTaintsDataFieldCallsiteCons("iot_security_cipher_params", 1, "write", "privleak");
   Constraint *privkleak2_2 = generateTaintsDataFieldCallsiteCons("iot_security_cipher_params", 1, "send", "privleak");
   Constraint *privkleak2_3 = generateTaintsDataFieldCallsiteCons("iot_security_cipher_params", 1, "print", "privleak");
   Constraint *privkleak2_4 = generateTaintsDataFieldCallsiteCons("iot_security_cipher_params", 1, "dump", "privleak");
   // t_seckey field
   Constraint *privkleak3 = generateTaintsDataFieldCallsiteCons("iot_security_ecdh_params", 0 , "iot_bsp_debug", "privleak");
   Constraint *privkleak3_1 = generateTaintsDataFieldCallsiteCons("iot_security_ecdh_params", 0 , "write", "privleak");
   Constraint *privkleak3_2 = generateTaintsDataFieldCallsiteCons("iot_security_ecdh_params", 0 , "send", "privleak");
   Constraint *privkleak3_3 = generateTaintsDataFieldCallsiteCons("iot_security_ecdh_params", 0 , "print", "privleak");
   Constraint *privkleak3_4 = generateTaintsDataFieldCallsiteCons("iot_security_ecdh_params", 0 , "dump", "privleak");
   // key field
   Constraint *privkleak4 = generateTaintsDataFieldCallsiteCons("iot_net_connection", 6, "iot_bsp_debug", "privleak");
   Constraint *privkleak4_1 = generateTaintsDataFieldCallsiteCons("iot_net_connection", 6, "write", "privleak");
   Constraint *privkleak4_2 = generateTaintsDataFieldCallsiteCons("iot_net_connection", 6, "send", "privleak");
   Constraint *privkleak4_3 = generateTaintsDataFieldCallsiteCons("iot_net_connection", 6, "print", "privleak");
   Constraint *privkleak4_4 = generateTaintsDataFieldCallsiteCons("iot_net_connection", 6, "dump", "privleak");

   // IoT query 8
   // Google IoT C SDK

   // password field MQTT client password
   Constraint *gprivleak1 = generateTaintsDataFieldCallsiteCons("iotc_connection_data_s", 2, "write", "privleak");
   // crypto_key_union 
   Constraint *gprivleak2 = generateTaintsDataFieldCallsiteCons("iotc_crypto_key_data_t", 1, "write", "privleak");


   Constraint *connectdataleak1 = generateTernaryForCallsiteCons("fill_with_connect_data", "write", "conleak");
   Constraint *connectdataleak2 = generateTernaryForCallsiteCons("fill_with_connect_data", "dump", "conleak");
   Constraint *connectdataleak3 = generateTernaryForCallsiteCons("fill_with_connect_data", "send", "conleak");
   Constraint *connectdataleak4 = generateTernaryForCallsiteCons("fill_with_connect_data", "print", "conleak");
 
   // private key processing sources
   Constraint *privleak1 = generateTernaryForCallsiteCons("iotc_bsp_ecc", "write", "privleak");
   Constraint *privleak2 = generateTernaryForCallsiteCons("iotc_bsp_ecc", "dump", "privleak");
   Constraint *privleak3 = generateTernaryForCallsiteCons("iotc_bsp_ecc", "send", "privleak");
   Constraint *privleak4 = generateTernaryForCallsiteCons("iotc_bsp_ecc", "print", "privleak");
   Constraint *privleak5 = generateTernaryForCallsiteCons("iotc_create_iotcore_jwt", "write", "privleak2");
   Constraint *privleak6 = generateTernaryForCallsiteCons("iotc_create_iotcore_jwt", "dump", "privleak2");
   Constraint *privleak7 = generateTernaryForCallsiteCons("iotc_create_iotcore_jwt", "send", "privleak2");
   Constraint *privleak8 = generateTernaryForCallsiteCons("iotc_create_iotcore_jwt", "print", "privleak2");

   // IoT query 9
   // Amazon FreeRTOS    
   // pPrivateKey field
    Constraint *aprivkleak1 = generateTaintsDataFieldCallsiteCons("IotNetworkCredentials", 7, "log", "privkleak"); 
    Constraint *aprivkleak1_1 = generateTaintsDataFieldCallsiteCons("IotNetworkCredentials", 7, "write", "privkleak"); 
    Constraint *aprivkleak1_2 = generateTaintsDataFieldCallsiteCons("IotNetworkCredentials", 7, "dump", "privkleak"); 
    Constraint *aprivkleak1_3 = generateTaintsDataFieldCallsiteCons("IotNetworkCredentials", 7, "send", "privkleak"); 
    Constraint *aprivkleak1_4 = generateTaintsDataFieldCallsiteCons("IotNetworkCredentials", 7, "print", "privkleak"); 
    // uxDevicePrivateKey field
    Constraint *aprivkleak2 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 0, "log", "privkleak");
    Constraint *aprivkleak2_1 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 0, "write", "privkleak");
    Constraint *aprivkleak2_2 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 0, "dump", "privkleak");
    Constraint *aprivkleak2_3 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 0, "send", "privkleak");
    Constraint *aprivkleak2_4 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 0, "print", "privkleak");
    // pPrivateKey
    Constraint *aprivkleak3 = generateTaintsDataFieldCallsiteCons("IotHttpsConnectionInfo", 9, "log", "wifileak");
    Constraint *aprivkleak3_1 = generateTaintsDataFieldCallsiteCons("IotHttpsConnectionInfo", 9, "write", "wifileak");
    Constraint *aprivkleak3_2 = generateTaintsDataFieldCallsiteCons("IotHttpsConnectionInfo", 9, "dump", "wifileak");
    Constraint *aprivkleak3_3 = generateTaintsDataFieldCallsiteCons("IotHttpsConnectionInfo", 9, "send", "wifileak");
    Constraint *aprivkleak3_4 = generateTaintsDataFieldCallsiteCons("IotHttpsConnectionInfo", 9, "print", "wifileak");
    // uxDevicePrivateKey
    Constraint *aprivkleak4 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 0, "log", "wifileak");
    Constraint *aprivkleak4_1 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 0, "write", "wifileak");
    Constraint *aprivkleak4_2 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 0, "dump", "wifileak");
    Constraint *aprivkleak4_3 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 0, "send", "wifileak");
    Constraint *aprivkleak4_4 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 0, "print", "wifileak");
    // uxCodeVerifyKey for over-the-air update
    Constraint *aprivkleak5 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 2, "log", "wifileak");
    Constraint *aprivkleak5_1 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 2, "write", "wifileak");
    Constraint *aprivkleak5_2 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 2, "dump", "wifileak");
    Constraint *aprivkleak5_3 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 2, "send", "wifileak");
    Constraint *aprivkleak5_4 = generateTaintsDataFieldCallsiteCons("P11KeyConfig_t", 2, "print", "wifileak");


    // user private data 
    // pPrivData 
    Constraint *uprivdata1 = generateTaintsDataFieldCallsiteCons("IotHttpsAsyncInfo_t", 1, "log", "wifileak");
    Constraint *uprivdata1_1 = generateTaintsDataFieldCallsiteCons("IotHttpsAsyncInfo_t", 1, "write", "wifileak");
    Constraint *uprivdata1_2 = generateTaintsDataFieldCallsiteCons("IotHttpsAsyncInfo_t", 1, "dump", "wifileak");
    Constraint *uprivdata1_3 = generateTaintsDataFieldCallsiteCons("IotHttpsAsyncInfo_t", 1, "send", "wifileak");
    Constraint *uprivdata1_4 = generateTaintsDataFieldCallsiteCons("IotHttpsAsyncInfo_t", 1, "print", "wifileak");

    // ucPin field (BT Legacy)
    Constraint *pinkeyleak = generateTaintsDataFieldCallsiteCons("BTPinCode_t", 0, "log", "wifileak");
    Constraint *pinkeyleak_1 = generateTaintsDataFieldCallsiteCons("BTPinCode_t", 0, "write", "wifileak");
    Constraint *pinkeyleak_2 = generateTaintsDataFieldCallsiteCons("BTPinCode_t", 0, "dump", "wifileak");
    Constraint *pinkeyleak_3 = generateTaintsDataFieldCallsiteCons("BTPinCode_t", 0, "send", "wifileak");
    Constraint *pinkeyleak_4 = generateTaintsDataFieldCallsiteCons("BTPinCode_t", 0, "print", "wifileak");

    // xPassword field
    Constraint *awifileak1 = generateTaintsDataFieldCallsiteCons("WIFINetworkParams_t", 3, "log", "wifileak"); 
    Constraint *awifileak1_1 = generateTaintsDataFieldCallsiteCons("WIFINetworkParams_t", 3, "write", "wifileak"); 
    Constraint *awifileak1_2 = generateTaintsDataFieldCallsiteCons("WIFINetworkParams_t", 3, "dump", "wifileak"); 
    Constraint *awifileak1_3 = generateTaintsDataFieldCallsiteCons("WIFINetworkParams_t", 3, "send", "wifileak"); 
    Constraint *awifileak1_4 = generateTaintsDataFieldCallsiteCons("WIFINetworkParams_t", 3, "print", "wifileak"); 
    // cPassword field
    Constraint *awifileak2 = generateTaintsDataFieldCallsiteCons("WIFINetworkProfile_t", 3,  "log", "wifileak");
    Constraint *awifileak2_1 = generateTaintsDataFieldCallsiteCons("WIFINetworkProfile_t", 3,  "write", "wifileak");
    Constraint *awifileak2_2 = generateTaintsDataFieldCallsiteCons("WIFINetworkProfile_t", 3,  "dump", "wifileak");
    Constraint *awifileak2_3 = generateTaintsDataFieldCallsiteCons("WIFINetworkProfile_t", 3,  "send", "wifileak");
    Constraint *awifileak2_4 = generateTaintsDataFieldCallsiteCons("WIFINetworkProfile_t", 3,  "print", "wifileak"); 
    // cKey
    Constraint *awifileak3 = generateTaintsDataFieldCallsiteCons("WIFIWEPKey_t", 0, "log", "wifileak");
    Constraint *awifileak3_1 = generateTaintsDataFieldCallsiteCons("WIFIWEPKey_t", 0, "write", "wifileak");
    Constraint *awifileak3_2 = generateTaintsDataFieldCallsiteCons("WIFIWEPKey_t", 0, "dump", "wifileak");
    Constraint *awifileak3_3 = generateTaintsDataFieldCallsiteCons("WIFIWEPKey_t", 0, "send", "wifileak");
    Constraint *awifileak3_4 = generateTaintsDataFieldCallsiteCons("WIFIWEPKey_t", 0, "print", "wifileak");
    // cPassphrase
    Constraint *awifileak4 = generateTaintsDataFieldCallsiteCons("WIFIWPAPassphrase_t", 0, "log", "wifileak");
    Constraint *awifileak4_1 = generateTaintsDataFieldCallsiteCons("WIFIWPAPassphrase_t", 0, "write", "wifileak");
    Constraint *awifileak4_2 = generateTaintsDataFieldCallsiteCons("WIFIWPAPassphrase_t", 0, "dump", "wifileak");
    Constraint *awifileak4_3 = generateTaintsDataFieldCallsiteCons("WIFIWPAPassphrase_t", 0, "send", "wifileak");
    Constraint *awifileak4_4 = generateTaintsDataFieldCallsiteCons("WIFIWPAPassphrase_t", 0, "print", "wifileak");

   // Wifi password leak   
   Constraint *aconleak1 = generateTernaryForCallsiteCons("parseConnect", "log", "conleak");
   Constraint *aconleak1_1 = generateTernaryForCallsiteCons("parseConnect", "write", "conleak");
   Constraint *aconleak1_2 = generateTernaryForCallsiteCons("parseConnect", "dump", "conleak");
   Constraint *aconleak1_3 = generateTernaryForCallsiteCons("parseConnect", "send", "conleak");
   Constraint *aconleak1_4 = generateTernaryForCallsiteCons("parseConnect", "print", "conleak");

   Constraint *aconleak2 = generateTernaryForCallsiteCons("_addNewNetwork", "log", "conleak");
   Constraint *aconleak2_1 = generateTernaryForCallsiteCons("_addNewNetwork", "write", "conleak");
   Constraint *aconleak2_2 = generateTernaryForCallsiteCons("_addNewNetwork", "dump", "conleak");
   Constraint *aconleak2_3 = generateTernaryForCallsiteCons("_addNewNetwork", "send", "conleak");
   Constraint *aconleak2_4 = generateTernaryForCallsiteCons("_addNewNetwork", "print", "conleak");

   Constraint *aconleak3 = generateTernaryForCallsiteCons("MQTT_SerializeConnect", "log", "conleak");
   Constraint *aconleak3_1 = generateTernaryForCallsiteCons("MQTT_SerializeConnect", "write", "conleak");
   Constraint *aconleak3_2 = generateTernaryForCallsiteCons("MQTT_SerializeConnect", "dump", "conleak");
   Constraint *aconleak3_3 = generateTernaryForCallsiteCons("MQTT_SerializeConnect", "send", "conleak");
   Constraint *aconleak3_4 = generateTernaryForCallsiteCons("MQTT_SerializeConnect", "print", "conleak");

   Constraint *aconleak4 = generateTernaryForCallsiteCons("MQTT_Connect", "log", "conleak"); 
   Constraint *aconleak4_1 = generateTernaryForCallsiteCons("MQTT_Connect", "write", "conleak"); 
   Constraint *aconleak4_2 = generateTernaryForCallsiteCons("MQTT_Connect", "dump", "conleak"); 
   Constraint *aconleak4_3 = generateTernaryForCallsiteCons("MQTT_Connect", "send", "conleak"); 
   Constraint *aconleak4_4 = generateTernaryForCallsiteCons("MQTT_Connect", "print", "conleak"); 

   Constraint *aconleak5 = generateTernaryForCallsiteCons("ES_WIFI_Connect", "log", "conleak");
   Constraint *aconleak5_1 = generateTernaryForCallsiteCons("ES_WIFI_Connect", "write", "conleak");
   Constraint *aconleak5_2 = generateTernaryForCallsiteCons("ES_WIFI_Connect", "dump", "conleak");
   Constraint *aconleak5_3 = generateTernaryForCallsiteCons("ES_WIFI_Connect", "send", "conleak");
   Constraint *aconleak5_4 = generateTernaryForCallsiteCons("ES_WIFI_Connect", "print", "conleak");


   for(auto lbm : labelMap) {
      if (!enableMap[lbm.first]) continue;
      Solution *sol = this->solve(lbm.second);
      if (sol) {
         llvm::errs() << "Solution for " << lbm.second->getDescription() << "\n";
         sol->print();
      }
      else
        llvm::errs() << "No solution for " << lbm.second->getDescription() << "\n";
   }

}
