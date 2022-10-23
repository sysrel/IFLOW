/*
 * DDAStat.cpp
 *
 *  Created on: Sep 15, 2014
 *      Author: Yulei Sui
 */

#include "DDA/DDAStat.h"
#include "DDA/FlowDDA.h"
#include "DDA/ContextDDA.h"
#include "MemoryModel/PointerAnalysis.h"
#include "Util/SVFUtil.h"
#include "Util/ICFGNode.h"
#include "Util/ICFG.h"
#include "MSSA/SVFGStat.h"
#include "MSSA/MemRegion.h"
#include "MSSA/MemSSA.h"
#include <llvm/Support/CommandLine.h> 
#include "MemoryModel/PAG.h"
#include "InfoFlow/InfoFlow.h"
#include <iomanip>
#include <fstream>
#define DBGP 0

using namespace SVFUtil;

char *inputFuncNames[] = {"socket_recv", "recv", "recvmsg", "recvfrom", "mbedtls_net_recv",  /*amazonFreeRTOS */
                         "lwip_recvfrom" , "netconn_recv", "lwip_recvmsg", 
                         "recvmsg", "lwip_recvfrom_udp_raw", "lwip_recv_tcp",
                         "netconn_recv_udp_raw_netbuf_flags", "udp6_socket_recv",
                         "tcp_recv", "ipv6_transport_recv", "netconn_recv", 
                         "ping_recv", "lwip_recvfrom", "sx_ulpgn_tcp_recv", 
                         "lwip_recv", "netconn_recv_tcp_pbuf_flags", 
                         "netconn_tcp_recvd", "lwip_recv_tcp_from", 
                         "lwip_recvfrom_udp_raw", "lwip_recv_tcp", "recvExact", 
                         "SOCKETS_Recv", "SecureSocketsTransport_Recv", "TLS_Recv", 
                         "transportRecv", "mbedtls_ssl_read", "receivePacket", 
                          NULL};

char *outputFuncNames[] = {"send", "sendmsg", "sendto", "mbedtls_net_send", /* amazonFreeRTOS*/
                          "lwip_send", "lwip_write", "lwip_sendmsg", "lwip_sendto", 
                          "netconn_send", "FreeRTOS_send", "FreeRTOS_sendto", 
                          "udp_send", "udp_sendto", "udp6_socket_send", "udp6_socket_sendto", 
                          "lwip_transport_send", "pal_socket_send", "pal_socket_sendto", 
                          "wiced_tcp_send_packet", "wiced_tcp_send_buffer", "wiced_udp_send", 
                          "network_tcp_send_packet", "netconn_send", "internal_udp_send", 
                          "ping_send", "wiced_rtos_send_asynchronous_event", "sendPacket", 
                          "_sendPuback", "mbedtls_ssl_send_fatal_handshake_failure", 
                          "mbedtls_ssl_send_alert_message", "SOCKETS_Send", 
                          "mbedtls_ssl_write", "sendPacket", NULL};

static llvm::cl::opt<std::string> EntryPointsFile("entry-points-file",
       llvm::cl::desc("<list of entry function names>"), llvm::cl::init(""));

llvm::cl::opt<bool> LearnTargetResolutions("learn-target-resolutions",
        llvm::cl::desc("<option to learn target resolutions for function pointers>"), 
        llvm::cl::init(false)); 

//llvm::cl::opt<bool> InfoFlow("info-flow",
//        llvm::cl::desc("<option to enable information flow query engine\n"),
//        llvm::cl::init(false));

std::set<std::string> entryFunctions;

DDAStat::DDAStat(FlowDDA* pta) : PTAStat(pta), flowDDA(pta), contextDDA(NULL) {
    initDefault();
}
DDAStat::DDAStat(ContextDDA* pta) : PTAStat(pta), flowDDA(NULL), contextDDA(pta) {
    initDefault();
}

void DDAStat::initDefault() {
    _TotalNumOfQuery = 0;
    _TotalNumOfOutOfBudgetQuery = 0;
    _TotalNumOfDPM = 0;
    _TotalNumOfStrongUpdates = 0;
    _TotalNumOfMustAliases = 0;
    _TotalNumOfInfeasiblePath = 0;
    _TotalNumOfStep = 0;
    _TotalNumOfStepInCycle = 0;

    _NumOfIndCallEdgeSolved = 0;
    _MaxCPtsSize = _MaxPtsSize = 0;
    _TotalCPtsSize = _TotalPtsSize = 0;
    _NumOfNullPtr = 0;
    _NumOfConstantPtr = 0;
    _NumOfBlackholePtr = 0;
    _AvgNumOfDPMAtSVFGNode = 0;
    _MaxNumOfDPMAtSVFGNode = 0;
    _TotalTimeOfQueries = 0;
    _AnaTimePerQuery = 0;
    _AnaTimeCyclePerQuery = 0;


    _NumOfDPM = 0;
    _NumOfStrongUpdates = 0;
    _NumOfMustAliases = 0;
    _NumOfInfeasiblePath = 0;

    _NumOfStep = 0;
    _NumOfStepInCycle = 0;
    _AnaTimePerQuery = 0;
    _AnaTimeCyclePerQuery = 0;
    _TotalTimeOfQueries = 0;

    _vmrssUsageBefore = _vmrssUsageAfter = 0;
    _vmsizeUsageBefore = _vmsizeUsageAfter = 0;
}

SVFG* DDAStat::getSVFG() const {
    if(flowDDA)
        return flowDDA->getSVFG();
    else
        return contextDDA->getSVFG();

}

PointerAnalysis* DDAStat::getPTA() const {
    if(flowDDA)
        return flowDDA;
    else
        return contextDDA;
}

void DDAStat::performStatPerQuery(NodeID ptr) {

    u32_t NumOfDPM = 0;
    u32_t NumOfLoc = 0;
    u32_t maxNumOfDPMPerLoc = 0;
    u32_t cptsSize = 0;
    PointsTo pts;
    if(flowDDA) {
        for(FlowDDA::LocToDPMVecMap::const_iterator it =  flowDDA->getLocToDPMVecMap().begin(),
                eit = flowDDA->getLocToDPMVecMap().end(); it!=eit; ++it) {
            NumOfLoc++;
            u32_t num = it->second.size();
            NumOfDPM += num;
            if(num > maxNumOfDPMPerLoc)
                maxNumOfDPMPerLoc = num;
        }
        cptsSize = flowDDA->getPts(ptr).count();
        pts = flowDDA->getPts(ptr);
    }
    else if(contextDDA) {
        for(ContextDDA::LocToDPMVecMap::const_iterator it =  contextDDA->getLocToDPMVecMap().begin(),
                eit = contextDDA->getLocToDPMVecMap().end(); it!=eit; ++it) {
            NumOfLoc++;
            u32_t num = it->second.size();
            NumOfDPM += num;
            if(num > maxNumOfDPMPerLoc)
                maxNumOfDPMPerLoc = num;
        }
        ContextCond cxt;
        CxtVar var(cxt,ptr);
        cptsSize = contextDDA->getPts(var).count();
        pts = contextDDA->getBVPointsTo(contextDDA->getPts(var));
    }
    u32_t ptsSize = pts.count();

    double avgDPMAtLoc = NumOfLoc!=0 ? (double)NumOfDPM/NumOfLoc : 0;
    _AvgNumOfDPMAtSVFGNode += avgDPMAtLoc;
    if(maxNumOfDPMPerLoc > _MaxNumOfDPMAtSVFGNode)
        _MaxNumOfDPMAtSVFGNode = maxNumOfDPMPerLoc;

    _TotalCPtsSize += cptsSize;
    if (_MaxCPtsSize < cptsSize)
        _MaxCPtsSize = cptsSize;

    _TotalPtsSize += ptsSize;
    if(_MaxPtsSize < ptsSize)
        _MaxPtsSize = ptsSize;

    if(cptsSize == 0)
        _NumOfNullPtr++;

    if(getPTA()->containBlackHoleNode(pts)) {
        _NumOfConstantPtr++;
    }
    if(getPTA()->containConstantNode(pts)) {
        _NumOfBlackholePtr++;
    }

    _TotalNumOfQuery++;
    _TotalNumOfDPM += _NumOfDPM;
    _TotalNumOfStrongUpdates += _NumOfStrongUpdates;
    _TotalNumOfMustAliases += _NumOfMustAliases;
    _TotalNumOfInfeasiblePath += _NumOfInfeasiblePath;

    _TotalNumOfStep += _NumOfStep;
    _TotalNumOfStepInCycle += _NumOfStepInCycle;

    timeStatMap.clear();
    NumPerQueryStatMap.clear();

    timeStatMap["TimePerQuery"] = _AnaTimePerQuery/TIMEINTERVAL;
    timeStatMap["CyleTimePerQuery"] = _AnaTimeCyclePerQuery/TIMEINTERVAL;

    NumPerQueryStatMap["CPtsSize"] = cptsSize;
    NumPerQueryStatMap["PtsSize"] = ptsSize;
    NumPerQueryStatMap["NumOfStep"] = _NumOfStep;
    NumPerQueryStatMap["NumOfStepInCycle"] = _NumOfStepInCycle;
    NumPerQueryStatMap["NumOfDPM"] = _NumOfDPM;
    NumPerQueryStatMap["NumOfSU"] = _NumOfStrongUpdates;
    NumPerQueryStatMap["IndEdgeResolved"] = getPTA()->getNumOfResolvedIndCallEdge() - _NumOfIndCallEdgeSolved;
    NumPerQueryStatMap["AvgDPMAtLoc"] = avgDPMAtLoc;
    NumPerQueryStatMap["MaxDPMAtLoc"] = maxNumOfDPMPerLoc;
    NumPerQueryStatMap["MaxPathPerQuery"] = VFPathCond::maximumPath;
    NumPerQueryStatMap["MaxCxtPerQuery"] = ContextCond::maximumCxt;
    NumPerQueryStatMap["NumOfMustAA"] = _NumOfMustAliases;
    NumPerQueryStatMap["NumOfInfePath"] = _NumOfInfeasiblePath;

    /// reset numbers for next query
    _NumOfStep = 0;
    _NumOfStepInCycle = 0;
    _NumOfDPM = 0;
    _NumOfStrongUpdates = 0;
    _NumOfMustAliases = 0;
    _NumOfInfeasiblePath = 0;
    _AnaTimeCyclePerQuery = 0;
    _NumOfIndCallEdgeSolved = getPTA()->getNumOfResolvedIndCallEdge();
}

void DDAStat::getNumOfOOBQuery()
{
    if (flowDDA)
        _TotalNumOfOutOfBudgetQuery = flowDDA->outOfBudgetDpms.size();
    else if (contextDDA)
        _TotalNumOfOutOfBudgetQuery = contextDDA->outOfBudgetDpms.size();
}

void DDAStat::performStat() {

    generalNumMap.clear();
    PTNumStatMap.clear();
    timeStatMap.clear();

    callgraphStat();

    getNumOfOOBQuery();

    for (PAG::const_iterator nodeIt = PAG::getPAG()->begin(), nodeEit = PAG::getPAG()->end(); nodeIt != nodeEit; nodeIt++) {
        PAGNode* pagNode = nodeIt->second;
        if(SVFUtil::isa<ObjPN>(pagNode)) {
            if(getPTA()->isLocalVarInRecursiveFun(nodeIt->first)) {
                localVarInRecursion.set(nodeIt->first);
            }
        }
    }

    timeStatMap["TotalQueryTime"] =  _TotalTimeOfQueries/TIMEINTERVAL;
    timeStatMap["AvgTimePerQuery"] =  (_TotalTimeOfQueries/TIMEINTERVAL)/_TotalNumOfQuery;
    timeStatMap["TotalBKCondTime"] =  (_TotalTimeOfBKCondition/TIMEINTERVAL);

    PTNumStatMap["NumOfQuery"] = _TotalNumOfQuery;
    PTNumStatMap["NumOfOOBQuery"] = _TotalNumOfOutOfBudgetQuery;
    PTNumStatMap["NumOfDPM"] = _TotalNumOfDPM;
    PTNumStatMap["NumOfSU"] = _TotalNumOfStrongUpdates;
    PTNumStatMap["NumOfStoreSU"] = _StrongUpdateStores.count();
    PTNumStatMap["NumOfStep"] =  _TotalNumOfStep;
    PTNumStatMap["NumOfStepInCycle"] =  _TotalNumOfStepInCycle;
    timeStatMap["AvgDPMAtLoc"] = (double)_AvgNumOfDPMAtSVFGNode/_TotalNumOfQuery;
    PTNumStatMap["MaxDPMAtLoc"] = _MaxNumOfDPMAtSVFGNode;
    PTNumStatMap["MaxPathPerQuery"] = VFPathCond::maximumPath;
    PTNumStatMap["MaxCxtPerQuery"] = ContextCond::maximumCxt;
    PTNumStatMap["MaxCPtsSize"] = _MaxCPtsSize;
    PTNumStatMap["MaxPtsSize"] = _MaxPtsSize;
    timeStatMap["AvgCPtsSize"] = (double)_TotalCPtsSize/_TotalNumOfQuery;
    timeStatMap["AvgPtsSize"] = (double)_TotalPtsSize/_TotalNumOfQuery;
    PTNumStatMap["IndEdgeSolved"] = getPTA()->getNumOfResolvedIndCallEdge();
    PTNumStatMap["NumOfNullPtr"] = _NumOfNullPtr;
    PTNumStatMap["PointsToConstPtr"] = _NumOfConstantPtr;
    PTNumStatMap["PointsToBlkPtr"] = _NumOfBlackholePtr;
    PTNumStatMap["NumOfMustAA"] = _TotalNumOfMustAliases;
    PTNumStatMap["NumOfInfePath"] = _TotalNumOfInfeasiblePath;
    PTNumStatMap["NumOfStore"] = PAG::getPAG()->getPTAEdgeSet(PAGEdge::Store).size();
    PTNumStatMap["MemoryUsageVmrss"] = _vmrssUsageAfter - _vmrssUsageBefore;
    PTNumStatMap["MemoryUsageVmsize"] = _vmsizeUsageAfter - _vmsizeUsageBefore;

    printStat();

    /* SYSREL extension */
    learnCandidateTargetResolutions();
    //if (InfoFlow)
    //   performInfoFlow();
    //findReachingSendWithoutReachingRecv();
    //featureExtraction();
    //findPatterns();
    //customStat();
    /* SYSREL extension */

}

void DDAStat::printStatPerQuery(NodeID ptr, const PointsTo& pts) {

    if (timeStatMap.empty() == false && NumPerQueryStatMap.empty() == false) {
        std::cout.flags(std::ios::left);
        unsigned field_width = 20;
        std::cout << "---------------------Stat Per Query--------------------------------\n";
        for (TIMEStatMap::iterator it = timeStatMap.begin(), eit = timeStatMap.end(); it != eit; ++it) {
            // format out put with width 20 space
            std::cout << std::setw(field_width) << it->first << it->second << "\n";
        }
        for (NUMStatMap::iterator it = NumPerQueryStatMap.begin(), eit = NumPerQueryStatMap.end(); it != eit; ++it) {
            // format out put with width 20 space
            std::cout << std::setw(field_width) << it->first << it->second << "\n";
        }
    }
    getPTA()->dumpPts(ptr, pts);
}


void DDAStat::printStat() {

    if(flowDDA) {
        FlowDDA::ConstSVFGEdgeSet edgeSet;
        flowDDA->getSVFG()->getStat()->performSCCStat(edgeSet);
    }
    else if(contextDDA) {
        contextDDA->getSVFG()->getStat()->performSCCStat(contextDDA->getInsensitiveEdgeSet());
    }

    std::cout << "\n****Demand-Driven Pointer Analysis Statistics****\n";
    PTAStat::printStat();
}

void DDAStat::getDependencies(llvm::User *v, std::set<const Instruction*> &neg, std::set<const Instruction*> var) {
   llvm::errs() << " getting dependencies for " << (*v) << "\n";
   for(User::op_iterator it = v->op_begin(); it != v->op_end(); it++) {
      if (GetElementPtrInst *gep = SVFUtil::dyn_cast<GetElementPtrInst>(&(*it))) {
         const DataLayout &DL = gep->getParent()->getParent()->getParent()->getDataLayout();
         unsigned BitWidth = DL.getPointerSizeInBits(DL.getPointerSize());
         bool offsetmatches = true;
         llvm::APInt llvmoffset(BitWidth, 0);
         if (!gep->hasAllConstantIndices()) {
            var.insert(gep);
         } 
         else if (gep->accumulateConstantOffset(DL, llvmoffset)) {
            if (llvmoffset.getSExtValue() < 0) 
               neg.insert(gep);
               return;
         }
      }
      for(Value::user_iterator uit = v->user_begin(); uit != v->user_end(); uit++)  {
           Value *vv = *uit;
           if (User *uu = SVFUtil::dyn_cast<User>(vv))
              getDependencies(uu, neg, var);
      }
   }
  
}

void DDAStat::printPtsInfo(PAGNode *pn) {
   if (pn->isPointer()) {
     SVFG* graph = getSVFG();
     PAG *pag = graph->getPAG(); 
     PointerAnalysis *pta = getPTA();
     NodeID src = pn->getId();
     const PointsTo& srcPts = pta->getPts(src);
     bool src_ptsblackhole = false;
     bool src_fieldinsensitive = false;
     bool src_isnullptr = false;
     for (PointsTo::iterator piter = srcPts.begin(); piter != srcPts.end(); ++piter) {
         NodeID ptd = *piter;
         #ifdef DBGPR
         llvm::errs() << "src points to: " << ptd << "\n"; 
         #endif
         if (pag->isBlkObj(ptd)) {
            src_ptsblackhole = true;
         }
         if ((flowDDA && flowDDA->isFieldInsensitive(ptd)) || 
             (contextDDA && contextDDA->isFieldInsensitive(ptd)))
            src_fieldinsensitive = true;
         if (pag->isNullPtr(ptd))
            src_isnullptr = true;
     }
     #ifdef DBGPR
     llvm::errs() << ",srcptssize=" << srcPts.count() 
                  << ",srcptstoblackhole=" << src_ptsblackhole 
                  << ",srcnullptr=" << src_isnullptr 
                  << ",srcfieldinsensitive=" << src_fieldinsensitive 
                  << "\n";
     #endif
   }
}      


bool DDAStat::violatesSoundness(NodeID q, const CallSite &cs, unsigned instanceno) {
    SVFG* graph = getSVFG();
    PAG *pag = graph->getPAG();
    SVFG::SVFGNodeIDToNodeMapTy::iterator it = graph->begin();
    SVFG::SVFGNodeIDToNodeMapTy::iterator eit = graph->end();
    for (; it != eit; ++it) {
        if (SVFUtil::isa<GepSVFGNode>(it->second)) {
            const GepSVFGNode *gepnode = SVFUtil::dyn_cast<GepSVFGNode>(it->second);
            #ifdef DBGPR 
            llvm::errs() << "gepnode: " << (*gepnode) << "\n";
            #endif
            NodeID src = gepnode->getPAGSrcNodeID();            
            PAGNode *srcnode = pag->getPAGNode(src);
            const Value *vs = srcnode->getValue();
            NodeID dst = gepnode->getPAGDstNodeID();
            PAGNode *dstnode = pag->getPAGNode(dst);
            if (q == src || q == dst) {
               PointerAnalysis *pta = getPTA();
               const PointsTo& srcPts = pta->getPts(src);
               const PointsTo& dstPts = pta->getPts(dst);
               const Value *vd = dstnode->getValue();
               bool samePtsSetDiffOffset = false;
               const GetElementPtrInst *gep = SVFUtil::dyn_cast<GetElementPtrInst>(vd);
               assert(gep);
               const Function *csfunc = cs.getInstruction()->getParent()->getParent();
               const Module *csm = csfunc->getParent();
               const Function *gfunc = gep->getParent()->getParent();
               const Module *gm = gfunc->getParent();
               const DataLayout &DL = gm->getDataLayout();
               unsigned BitWidth = DL.getPointerSizeInBits(DL.getPointerSize());
               llvm::APInt llvmoffset(BitWidth, 0);
               if (gep->accumulateConstantOffset(DL, llvmoffset)) {
                  if (llvmoffset.getSExtValue() != 0 && srcPts == dstPts) {
                     // of course ptsto sets may match if offset is a zero
                     samePtsSetDiffOffset = true;    
                  }
               }
               bool emptyPtsToForDst = srcPts.count() > 0 && dstPts.count() == 0;
               if (samePtsSetDiffOffset || emptyPtsToForDst) {
                  std::string violationType = "unknown";
                  if (samePtsSetDiffOffset)
                     violationType = "sameSetDiffOffset";
                  if (emptyPtsToForDst)
                     violationType = "emptyPtsForDst";
                  #ifdef DBGPR
                  llvm::errs() << "dependent flow is part of Gep with identical src-dst pointsto set!\n";
                  #endif
                  std::string geptype = "positive";
                  for(User::const_op_iterator oi = gep->idx_begin(), 
                               oe = gep->idx_end(); oi != oe; oi++) {
                     Value *v = oi->get();
                     if (llvm::ConstantInt* CI = SVFUtil::dyn_cast<llvm::ConstantInt>(v)) {
                        if (CI->getSExtValue() < 0) {
                           geptype = "negative";
                        }
                     }
                     else {
                        geptype = "variable";
                     }
                  }
                  std::string csfilename = csm->getName();
                  std::string csfuncname = csfunc->getName();
                  std::string gepfilename = gm->getName();
                  std::string gepfuncname = gfunc->getName();
                  unsigned geplineno = 0;
                  llvm::MDNode *gmd = gep->getMetadata("dbg");
                  if (gmd) {
                     llvm::DebugLoc loc(gmd);
                     geplineno = loc.getLine();
                  }
                  unsigned cslineno = 0;
                  llvm::MDNode *csmd = cs.getInstruction()->getMetadata("dbg");
                  if (csmd) {
                     llvm::DebugLoc loc(csmd);
                     cslineno = loc.getLine();
                  }
                  llvm::errs() << instanceno << ","
                               << "\"" << (*cs.getInstruction()) << "\","
                               << csfuncname << ","
                               << csfilename << ","
                               << cslineno << ","
                               << "\"" << (*gep) << "\"," 
                               << violationType << "," 
                               << geptype << "," 
                               << gepfuncname << "," 
                               << gepfilename << ","
                               << geplineno << "\n"; 
                  return true;
               }
            }
        }
    }
    return false;
}

void DDAStat::getVFGDependencies(const User *u, const CallSite &cs, unsigned instanceno) {
   //llvm::errs() << "Value-flow graph dependencies of " << (*u) << "\n";
   SVFG* graph = getSVFG();
   PAG *pag = graph->getPAG(); 
   PAGNode *pn = pag->getPAGNode(pag->getValueNode(u));
   violatesSoundness(pn->getId(),cs,instanceno);
   const VFGNode *vn = graph->getDefVFGNode(pn);
   const SVFGEdge::SVFGEdgeSetTy& inEdges = vn->getInEdges();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = inEdges.begin();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = inEdges.end();
   for (; edgeIt != edgeEit; ++edgeIt) {
       const SVFGEdge *edge = *edgeIt;
       NodeID si = edge->getSrcID();
       SVFGNode *sn = graph->getSVFGNode(si);
       if (StmtVFGNode *stn = SVFUtil::dyn_cast<StmtVFGNode>(sn)) {
          NodeID pi = stn->getPAGSrcNodeID();            
          PAGNode *srcnode = pag->getPAGNode(pi);
          const Value *vs = srcnode->getValue();
          #ifdef DBGPR
          llvm::errs() << "dependent on:" << (*vs) << "\n";
          #endif
          printPtsInfo(srcnode); 
          if (const User *usucc = SVFUtil::dyn_cast<User>(vs)) {
              getVFGDependencies(usucc,cs,instanceno);
          }
       }
   }
}

void visitReachableDefs(SVFGNode* orig, SVFGNode* node, 
                           std::set<SVFGNode*> visited) {
   if (visited.find(node) != visited.end())
      return;
   visited.insert(node);
   const SVFGEdge::SVFGEdgeSetTy& inEdges = node->getInEdges();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = inEdges.begin();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = inEdges.end();
   for (; edgeIt != edgeEit; ++edgeIt)
   {
       const SVFGEdge *edge = *edgeIt;
       VFGNode *src = dyn_cast<VFGNode>(edge->getSrcNode());
       StmtVFGNode *srcst = dyn_cast<StmtVFGNode>(src);
       StmtVFGNode *destst = dyn_cast<StmtVFGNode>(orig);
       assert(srcst);
       if (!srcst) {
          visitReachableDefs(orig, src, visited); 
       }
       else if (srcst->getInst()->getOpcode() == llvm::Instruction::Store) 
       {
          std::string ss = SVFUtil::getSourceLoc(srcst->getInst());
          std::string ds = SVFUtil::getSourceLoc(destst->getInst());
          if (ss != ds)
             llvm::errs() << "[Multistep] source: " << ss 
                            << " dest: " << ds << "\n";    
       }  
   }
   
}

void visitReachableUses(SVFGNode* orig, SVFGNode* node, 
                           std::set<SVFGNode*> &visited) {
   if (visited.find(node) != visited.end())
      return;
   visited.insert(node);
   const SVFGEdge::SVFGEdgeSetTy& outEdges = node->getOutEdges();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = outEdges.begin();
   SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = outEdges.end();
   for (; edgeIt != edgeEit; ++edgeIt)
   {
       const SVFGEdge *edge = *edgeIt;
       VFGNode *dest = dyn_cast<VFGNode>(edge->getDstNode());
       StmtVFGNode *srcst = dyn_cast<StmtVFGNode>(orig);
       StmtVFGNode *destst = dyn_cast<StmtVFGNode>(dest);
       assert(srcst);
       if (!destst) {
          visitReachableUses(orig, dest, visited); 
       }
       else 
       {
          std::string ss = SVFUtil::getSourceLoc(srcst->getInst());
          std::string ds = SVFUtil::getSourceLoc(destst->getInst());
          if (ss != ds && ss != "" && ds != ""
                     && ss != "empty val" && ds != "empty val")
             llvm::errs() << "[Store-Load] source: " << ss 
                          << " dest: " << ds << "\n";    
       }  
   }


   
}


bool isRecv(std::string fn) {
  for(int i=0; inputFuncNames[i]; i++)
     if (strcmp(fn.c_str(), inputFuncNames[i]) == 0)
        return true;
  return false;
}

bool isSend(std::string fn) {
  for(int i=0; outputFuncNames[i]; i++)
     if (strcmp(fn.c_str(), outputFuncNames[i]) == 0)
        return true;
  return false;
}

void DDAStat::learnCandidateTargetResolutions() {
  if (!LearnTargetResolutions) return;
  if (LearnTargetResolutions && EntryPointsFile == "")
     assert(0 && "Please specify the entry function names!\n");
  std::fstream ef(EntryPointsFile.c_str());
  if (ef.is_open()) {  
     std::string line;
     while (std::getline(ef, line)) {
          entryFunctions.insert(line);
     }
     ef.close();
  } 
 else {
    llvm::errs() << "Entry poins file could not be opened\n";
    return;
 }
 PointerAnalysis *pta = getPTA();
 PTACallGraph *cg = pta->getPTACallGraph();
 std::set<NodeID> entries, worklist;
 for(PTACallGraph::iterator it = cg->begin(); it != cg->end(); it++) {
    PTACallGraphNode *node = it->second;
    if (entryFunctions.find(node->getFunction()->getName()) != entryFunctions.end()) {
       worklist.insert(it->first);
       llvm::errs() << "Entry function in the call graph: " << node->getFunction()->getName() << "\n";
    }
  }

  int added = 0;
  do {
     added = 0;
     for(auto f : worklist) {
        PTACallGraphNode *node = cg->getCallGraphNode(f);
        for(PTACallGraphNode::iterator it = node->OutEdgeBegin(), eit = node->OutEdgeEnd(); 
                      it != eit; it++) {
            PTACallGraphEdge* edge = *it;
            llvm::errs() << edge->getSrcNode()->getFunction()->getName() << " -> " 
                         << node->getFunction()->getName() << "\n";
            if (worklist.find(edge->getDstNode()->getId()) == worklist.end()) {
               worklist.insert(edge->getDstNode()->getId());
               added++;
            }
        }
        
     }
  }
  while (added == 0);

  std::set<std::string> reachableFunctions;
  for(auto r: worklist) {
      PTACallGraphNode *node = cg->getCallGraphNode(r);
      reachableFunctions.insert(node->getFunction()->getName());
  }

  /*const PointerAnalysis::CallEdgeMap& callEdges = pta->getIndCallMap();
  PointerAnalysis::CallEdgeMap::const_iterator it = callEdges.begin();
  PointerAnalysis::CallEdgeMap::const_iterator eit = callEdges.end();
  for (; it != eit; ++it) {
      const CallSite cs = it->first;
      const std::set<const Function*> & targets = it->second;
      const llvm::Instruction *inst =cs.getInstruction();
      llvm::Function *func = (Function*)inst->getParent()->getParent();
      if (reachableFunctions.find(func->getName().str()) == reachableFunctions.end())
          continue;
      if (targets.empty()) {
           llvm::errs() << "Unresolved indirect call:," << 
                           SVFUtil::getSourceLoc(inst) << 
                           (*inst) << "\n"; 

      }
      else { 
             llvm::errs() << "Resolved indirect call:," << 
                           SVFUtil::getSourceLoc(inst) << 
                           (*inst) 
                          << " num targets=" << targets.size() << "\n"; 
             for(auto f: targets) 
                llvm::errs() << "Target: " << f->getName() << "\n";
      }
  }*/

  const PointerAnalysis::CallSiteToFunPtrMap& indCS = pta->getIndirectCallsites();
  PointerAnalysis::CallSiteToFunPtrMap::const_iterator csIt = indCS.begin();
  PointerAnalysis::CallSiteToFunPtrMap::const_iterator csEit = indCS.end();
  for (; csIt != csEit; ++csIt)
  {
        const CallSite &cs = csIt->first;
        const llvm::Instruction *inst = cs.getInstruction(); 
        const llvm::Function *f = (Function*)inst->getParent()->getParent();
        //if (reachableFunctions.find(f->getName()) == reachableFunctions.end())
        //   continue;                      
        if (cg->hasIndCSCallees(cs) == false)
        {
            llvm::errs() << "Unresolved indirect callsite:," << 
                           SVFUtil::getSourceLoc(inst) << 
                           (*inst) << " in function " << f->getName() << "\n";  
        }
        else  {
            const std::set<const Function*> &targets = cg->getIndCSCallees(cs);
            llvm::errs() << "Resolved indirect callsite:," << 
                           SVFUtil::getSourceLoc(inst) << 
                           (*inst) << " in function " << f->getName() 
                           << " num targets=" << targets.size() << "\n";  
            for(auto f: targets) 
                llvm::errs() << "Target: " << f->getName() << "\n";
        }
        llvm::errs() << "Basic block info: \n";
        const llvm::BasicBlock *bb= (BasicBlock*)inst->getParent();
        for(llvm::BasicBlock::const_iterator it = bb->begin(), eit = bb->end();
                   it != eit; it++) {
           llvm::errs() << (*it) << "\n";
        }
  }

}

void DDAStat::findReachingSendWithoutReachingRecv() {
   PointerAnalysis *pta = getPTA();
   PTACallGraph *cg = pta->getPTACallGraph();
   std::set<NodeID> recvs, sends;
   for(PTACallGraph::iterator it = cg->begin(); it != cg->end(); it++) {
      PTACallGraphNode *node = it->second;
      llvm::errs() << "Function " << node->getFunction()->getName() << "\n";
      if (isRecv(node->getFunction()->getName())) {
         recvs.insert(it->first);
         llvm::errs() << "Receiver function: " << node->getFunction()->getName() << "\n";

      }
      if (isSend(node->getFunction()->getName())) {
         sends.insert(it->first); 
         llvm::errs() << "Sender function: " << node->getFunction()->getName() << "\n";
      }
   }
       
   std::set<NodeID> rrecv = recvs;
   int added = 0;
   do {
      added = 0;
      for(auto r : rrecv) {
         PTACallGraphNode *node = cg->getCallGraphNode(r);
         for(PTACallGraphNode::iterator it = node->InEdgeBegin(), eit = node->InEdgeEnd(); 
                      it != eit; it++) {
            PTACallGraphEdge* edge = *it;
            llvm::errs() << edge->getSrcNode()->getFunction()->getName() << " -> " 
                         << node->getFunction()->getName() << "\n";
            if (rrecv.find(edge->getSrcNode()->getId()) == rrecv.end()) {
               rrecv.insert(edge->getSrcNode()->getId());
               added++;
            }
         }
      }
   }
   while (added != 0);

   std::set<NodeID> rsendnrecv = sends;

   do {
      added = 0; 
      for(auto s : rsendnrecv) {
         PTACallGraphNode *node = cg->getCallGraphNode(s);
         for(PTACallGraphNode::iterator it = node->InEdgeBegin(), eit = node->InEdgeEnd(); 
                      it != eit; it++) {
            PTACallGraphEdge* edge = *it;
            llvm::errs() << edge->getSrcNode()->getFunction()->getName() << " -> " 
                         << node->getFunction()->getName() << "\n";
            if (rsendnrecv.find(edge->getSrcNode()->getId()) == rsendnrecv.end() && 
                  rrecv.find(edge->getSrcNode()->getId()) == rrecv.end()) {
               rsendnrecv.insert(edge->getSrcNode()->getId());
               added++;
            }
         }
      }
   }
   while (added != 0);
   std::ofstream rf("reachingSendNotReceive.txt");
   std::ofstream rfm("reachingSendNotReceiveModel.txt");
   for(auto rs : rsendnrecv) {
      rf << cg->getCallGraphNode(rs)->getFunction()->getName().str() << "\n";
      rfm << "returnonly " << cg->getCallGraphNode(rs)->getFunction()->getName().str() << ";\n";
   }
   rf.close();
   rfm.close();
   std::ofstream recvf("reachingRecv.txt");
   for(auto rs : rrecv) {
      recvf << cg->getCallGraphNode(rs)->getFunction()->getName().str() << "\n";
   }
    recvf.close();
}


void DDAStat::performInfoFlow() {
   ICFG *icfg = new ICFG(getPTA()->getPTACallGraph());
   InfoFlowEngine *ifl = new InfoFlowEngine(getSVFG(), icfg);
   ifl->runQuery();
   /*RegExpQueryExp *dtq = new RegExpQueryExp(std::string("stype"), structT);
   dtq->addIncludes(std::string("work"));
   dtq->addDoesNotBeginWith(std::string("Re"));
   Query *dtq_query = new Query(dtq);
   Label *dtlabel = new Label(std::string("networkst"), dtq_query);
   llvm::errs() << "data entity id: " << dtq->getOperand() << "\n";
   llvm::errs() << "data entity id name: " << dtq->getOperand()->getName() << "\n";
   Constraint *dtcons = new Constraint(dtq->getOperand(), dtlabel);
   RegExpQueryExp *cbq = new RegExpQueryExp(std::string("cbfield"), funcptrfieldT);
   Query *cbq_query = new Query(cbq);
   Label *cblabel = new Label(std::string("cb"), cbq_query);
   llvm::errs() << "cb entity id: " << cbq->getOperand() << "\n";
   llvm::errs() << "cb entity id name: " << cbq->getOperand()->getName() << "\n";
   Constraint *cbcons = new Constraint(cbq->getOperand(), cblabel);
   RelationalQueryExp *cbdtq = new RelationalQueryExp(cbq->getOperand(), dtq->getOperand(), callbackdefinedin);
   Query *callbackq = new Query(cbdtq);
   callbackq->addConstraint(dtcons);
   callbackq->addConstraint(cbcons);
   Label *nwcb = new Label(std::string("networkcb"), callbackq);
   Solution *nwcbsol = ifl->solve(nwcb);  
   if (nwcbsol) {
      llvm::errs() << "Solution for " << nwcb->getDescription() << "\n";   
      nwcbsol->print();
   }
   else 
     llvm::errs() << "No solution for " << nwcb->getDescription() << "\n";  
   */

   // (recvarg taints memcpyarg)
   //          where  
   //                recvarg is a pointerFuncArgT 
   //                memcpyarg is a pointerFuncArgT
   //                recvarg actualparameterpassedby recvcallsite 
   //                    where recvcallsite is a callsiteT
   //                          recvcallsite indirectcallof recvcallback
   //                              where recvcallback is a funcptrfieldT
   //                                    recvcallback callbackdefinedin networkStruct
   //                                          where networkStruct is a structT
   //                                                networkStruct includes "Network"
   //                memcpyarg actualparameterpassedby  memcpycallsite
   //                    where memcpycallsite is a callsiteT
   //                          memcpycallsite equals memcpy

   // recvarg
   /*RegExpQueryExp *recvargqexp = new RegExpQueryExp(std::string("recvarg"), pointerFuncArgT);
   Query *recvargq = new Query(recvargqexp);
   Label *recvarglabel = new Label(std::string("recvargLabel"), recvargq);
   Solution *s1 = ifl->solve(recvarglabel);
   if (s1) {
      llvm::errs() << "Solution for " << recvarglabel->getDescription() << "\n";
      s1->print();
   }
   Constraint *racons = new Constraint(recvargqexp->getOperand(), recvarglabel);  

   // recvcallsite
   RegExpQueryExp *recvcsqexp = new RegExpQueryExp(std::string("recvcallsite"), callsiteT);
   Query *recvcsq = new Query(recvcsqexp);
   Label *recvcslabel = new Label(std::string("recvcsLabel"), recvcsq);
   Solution *s2 = ifl->solve(recvcslabel);
   if (s2) {
      llvm::errs() << "Solution for " << recvcslabel->getDescription() << "\n";
      s2->print();
   }
   Constraint *recvcscons = new Constraint(recvcsqexp->getOperand(), recvcslabel);  

   // recvcallback
   RegExpQueryExp *recvcbqexp = new RegExpQueryExp(std::string("recvcallback"), funcptrfieldT);
   Query *recvcbq = new Query(recvcbqexp);
   Label *recvcblabel = new Label(std::string("recvcbLabel"), recvcbq);
   Solution *s3 = ifl->solve(recvcblabel);
   if (s3) {
      llvm::errs() << "Solution for " << recvcblabel->getDescription() << "\n";
      s3->print(); 
   }
   Constraint *recvcbcons = new Constraint(recvcbqexp->getOperand(), recvcblabel);

   // networkdts
   RegExpQueryExp *networkstqexp = new RegExpQueryExp(std::string("networkstruct"), structT);
   networkstqexp->setEquals("Network");
   Query *networkstq = new Query(networkstqexp);
   Label *networkstlabel = new Label(std::string("networkstLabel"), networkstq);
   Solution *s4 = ifl->solve(networkstlabel);
   if (s4) {
      llvm::errs() << "Solution for " << networkstlabel->getDescription() << "\n";
      s4->print(); 
   }
   Constraint *networkstcons = new Constraint(networkstqexp->getOperand(), networkstlabel);    

   // recvcallback callbackdefinedin networkStruct
   RelationalQueryExp *recvdefnetqexp = new RelationalQueryExp(recvcbqexp->getOperand(),
                                                               networkstqexp->getOperand(),
                                                               callbackdefinedin);
   Query *recvdefnetq = new Query(recvdefnetqexp);
   recvdefnetq->addConstraint(recvcbcons);
   recvdefnetq->addConstraint(networkstcons);
   Label *recvdefnetlabel = new Label(std::string("recvinnetworkLabel"), recvdefnetq);
   Solution *s5 = ifl->solve(recvdefnetlabel);
   if (s5) {
      llvm::errs() << "Solution for " << recvdefnetlabel->getDescription() << "\n";
      s5->print();
   }
   Constraint *recvdefnetcons = new Constraint(recvcbqexp->getOperand(), recvdefnetlabel);

   // recvcallsite indirectcallof recvcallback
   RelationalQueryExp *recvindrecvcbqexp = new RelationalQueryExp(recvcsqexp->getOperand(),
                                                                  recvcbqexp->getOperand(),
                                                                  indirectcallof);
   Query *recvindrecvcbq = new Query(recvindrecvcbqexp);
   recvindrecvcbq->addConstraint(recvcscons);
   recvindrecvcbq->addConstraint(recvdefnetcons);
   Label *recvindrecvcblabel = new Label(std::string("recvindrecvcbLabel"), recvindrecvcbq);
   Solution *s6 = ifl->solve(recvindrecvcblabel);
   if (s6) {
      llvm::errs() << "Solution for " << recvindrecvcblabel->getDescription() << "\n";
      s6->print();     
   }
   else llvm::errs() << "No solution for " << recvindrecvcblabel->getDescription() << "\n";
   Constraint *recvindrecvcbcons = new Constraint(recvcsqexp->getOperand(), recvindrecvcblabel);

   // recvarg actualparameterpassedby recvcallsite
   RelationalQueryExp *recvargpbrecvcsqexp = new RelationalQueryExp(recvargqexp->getOperand(),
                                                                    recvcsqexp->getOperand(),
                                                                    actualparameterpassedby);
   Query *recvargpbrecvcsq = new Query(recvargpbrecvcsqexp);
   recvargpbrecvcsq->addConstraint(racons);
   recvargpbrecvcsq->addConstraint(recvindrecvcbcons);
   Label *recvargpbrecvcslabel = new Label(std::string("recvargpbrecvcs"), recvargpbrecvcsq);
   Solution *s7 = ifl->solve(recvargpbrecvcslabel);
   if (s7) {
      llvm::errs() << "Solution for " << recvargpbrecvcslabel->getDescription() << "\n";
      s7->print();    
   }
   Constraint *recvargpbrecvcscons = new Constraint(recvargqexp->getOperand(), recvargpbrecvcslabel);

   // memcpyarg
   RegExpQueryExp *memcpyargqexp = new RegExpQueryExp(std::string("memcpyarg"), pointerFuncArgT);
   Query *memcpyargq = new Query(memcpyargqexp);
   Label *memcpyarglabel = new Label(std::string("memcpyargLabel"), memcpyargq);
   Solution *s8 = ifl->solve(memcpyarglabel);
   if (s8) {
      llvm::errs() << "Solution for " << memcpyarglabel->getDescription() << "\n";
      s8->print(); 
   }
   Constraint *macons = new Constraint(memcpyargqexp->getOperand(), memcpyarglabel);   

   // memcpy callsites
   RegExpQueryExp *memcpycsqexp = new RegExpQueryExp(std::string("memcopycs"), callsiteT);
   memcpycsqexp->addIncludes("memcpy"); 
   Query *memcpycsq = new Query(memcpycsqexp);
   Label *memcpycslabel = new Label(std::string("memcpycs"), memcpycsq);
   Solution *s9 = ifl->solve(memcpycslabel);
   if (s9) {
      llvm::errs() << "Solution for " << memcpycslabel->getDescription() << "\n";
      s9->print();
   }
   Constraint *mcscons = new Constraint(memcpycsqexp->getOperand(), memcpycslabel);

   // memcpyarg actualparameterpassedby  memcpycallsite  
   RelationalQueryExp *memcpyargpbmemcpycsqexp = new RelationalQueryExp(memcpyargqexp->getOperand(),
                                                                        memcpycsqexp->getOperand(),
                                                                        actualparameterpassedby);
   Query *memcpyargpbmemcpycsq = new Query(memcpyargpbmemcpycsqexp);
   memcpyargpbmemcpycsq->addConstraint(macons);
   memcpyargpbmemcpycsq->addConstraint(mcscons);
   Label *memcpyargpbmemcpycslabel = new Label(std::string("memcpyargpbmemcpycsLabel"), memcpyargpbmemcpycsq);
   Solution *s10 = ifl->solve(memcpyargpbmemcpycslabel);
   if (s10) {
      llvm::errs() << "Solution for " << memcpycslabel->getDescription() << "\n";
      s10->print();
   }
   Constraint *margmcscons = new Constraint(memcpyargqexp->getOperand(), memcpyargpbmemcpycslabel);

   // temparg
   RegExpQueryExp *tempargqexp = new RegExpQueryExp(std::string("temparg"), pointerFuncArgT);
   Query *tempargq = new Query(tempargqexp);
   Label *temparglabel = new Label(std::string("tempargLabel"), tempargq);
   Solution *st0 = ifl->solve(temparglabel);
   if (st0) {
      llvm::errs() << "Solution for " << temparglabel->getDescription() << "\n";
      st0->print();
   }
   Constraint *tacons = new Constraint(tempargqexp->getOperand(), temparglabel);

   // tempcs
   RegExpQueryExp *tempcsqexp = new RegExpQueryExp(std::string("tempcs"), callsiteT);
   tempcsqexp->addIncludes("foo");
   Query *tempcsq = new Query(tempcsqexp);
   Label *tempcslabel = new Label(std::string("tempcs"), tempcsq);
   Solution *st1 = ifl->solve(tempcslabel);
   if (st1) {
      llvm::errs() << "Solution for " << tempcslabel->getDescription() << "\n";
      st1->print();
   }
   Constraint *tempcscons = new Constraint(tempcsqexp->getOperand(), tempcslabel);

   // temparg actualparameterpassedby tempcs
   RelationalQueryExp *tempargpbtempcsqexp = new RelationalQueryExp(tempargqexp->getOperand(),
                                                                        tempcsqexp->getOperand(),
                                                                        actualparameterpassedby);
   Query *tempargpbtempcsq = new Query(tempargpbtempcsqexp);
   tempargpbtempcsq->addConstraint(tacons);
   tempargpbtempcsq->addConstraint(tempcscons);
   Label *tempargpbtempcslabel = new Label(std::string("tempargpbtempcsLabel"), tempargpbtempcsq);
   Solution *ts2 = ifl->solve(tempargpbtempcslabel);
   if (ts2) {
      llvm::errs() << "Solution for " << tempargpbtempcslabel->getDescription() << "\n";
      ts2->print();
   }
   Constraint *targtcscons = new Constraint(tempargqexp->getOperand(), tempargpbtempcslabel);

  // carg
   RegExpQueryExp *copyargqexp = new RegExpQueryExp(std::string("copyarg"), pointerFuncArgT);
   Query *copyargq = new Query(copyargqexp);
   Label *copyarglabel = new Label(std::string("copyargLabel"), tempargq);
   Solution *sc0 = ifl->solve(copyarglabel);
   if (sc0) {
      llvm::errs() << "Solution for " << copyarglabel->getDescription() << "\n";
      sc0->print();
   }
   Constraint *copyacons = new Constraint(copyargqexp->getOperand(), copyarglabel);

   // copycs
   RegExpQueryExp *copycsqexp = new RegExpQueryExp(std::string("copycs"), callsiteT);
   copycsqexp->addIncludes("copy");
   Query *copycsq = new Query(copycsqexp);
   Label *copycslabel = new Label(std::string("copycs"), copycsq);
   Solution *sc1 = ifl->solve(copycslabel);
   if (sc1) {
      llvm::errs() << "Solution for " << copycslabel->getDescription() << "\n";
      sc1->print();
   }
   Constraint *copycscons = new Constraint(copycsqexp->getOperand(), copycslabel);

   // copyarg actualparameterpassedby copycs
   RelationalQueryExp *copyargpbcopycsqexp = new RelationalQueryExp(copyargqexp->getOperand(),
                                                                        copycsqexp->getOperand(),
                                                                        actualparameterpassedby);
   Query *copyargpbcopycsq = new Query(copyargpbcopycsqexp);
   copyargpbcopycsq->addConstraint(copyacons);
   copyargpbcopycsq->addConstraint(copycscons);
   Label *copyargpbcopycslabel = new Label(std::string("copyargpbcopycsLabel"), copyargpbcopycsq);
   Solution *sc2 = ifl->solve(copyargpbcopycslabel);
   if (sc2) {
      llvm::errs() << "Solution for " << copyargpbcopycslabel->getDescription() << "\n";
      sc2->print();
   }
   Constraint *cargtcscons = new Constraint(copyargqexp->getOperand(), copyargpbcopycslabel);

   // temparg taints copyarg
   RelationalQueryExp *temptaintscopyqexp = new RelationalQueryExp(tempargqexp->getOperand(),
                                                                copyargqexp->getOperand(),
                                                                taints);
   Query *temptaintscopyq = new Query(temptaintscopyqexp);
   temptaintscopyq->addConstraint(targtcscons);
   temptaintscopyq->addConstraint(cargtcscons);
   Label *temptaintscopylabel = new Label(std::string("footaintscopy"), temptaintscopyq);

   Solution *tcsol = ifl->solve(temptaintscopylabel);
   if (tcsol) {
      llvm::errs() << "Solution for " << temptaintscopylabel->getDescription() << "\n";
      tcsol->print();
   }
   else
     llvm::errs() << "No solution for " << temptaintscopylabel->getDescription() << "\n";

   // The buffer updated by receive flows into memcpy as a source or dest buffer
   // recvarg taints memcpyarg
   RelationalQueryExp *recvtaintsmemcpyqexp = new RelationalQueryExp(recvargqexp->getOperand(), 
                                                                memcpyargqexp->getOperand(), 
                                                                taints); 
   Query *recvtaintsmemcpyq = new Query(recvtaintsmemcpyqexp);
   recvtaintsmemcpyq->addConstraint(recvargpbrecvcscons);   
   recvtaintsmemcpyq->addConstraint(margmcscons);   
   Label *recvtaintsmemcpylabel = new Label(std::string("recvtaintsmemcpy"), recvtaintsmemcpyq);   

   Solution *sol = ifl->solve(recvtaintsmemcpylabel);    
   if (sol) {
      llvm::errs() << "Solution for " << recvtaintsmemcpylabel->getDescription() << "\n";
      sol->print();
   }
   else
     llvm::errs() << "No solution for " << recvtaintsmemcpylabel->getDescription() << "\n";
  */
   
}

void DDAStat::findPatterns() {
        SVFG *graph = getSVFG();
/*
        std::string outf("svfg.txt",true);
        graph->dump(outf);
        SVFG::SVFGNodeIDToNodeMapTy::iterator it = graph->begin();
        SVFG::SVFGNodeIDToNodeMapTy::iterator eit = graph->end();
        PAG *pag = graph->getPAG();
        for(PAG::iterator iter = pag->begin(); 
                        iter != pag->end(); iter++) 
        {
           if (iter->second->hasValue() && iter->second->getValue()) 
           {
              const llvm::Instruction *inst = 
                    llvm::dyn_cast<llvm::Instruction>(iter->second->getValue());
              if (inst)  
                 llvm::errs() << SVFUtil::getSourceLoc(inst) << ":" 
                           << (*inst) << "\n";
           }
        }   
*/
}

std::map<PAGNode*,std::set<SVFGNode*> > mrdefs;
std::map<const Value*,std::set<const Instruction*> > pagdefs;

void DDAStat::featureExtraction() {
        /* Feature Extraction */
        SVFG *graph = getSVFG();
        std::string outf("svfg.txt",true);
        graph->dump(outf);
        SVFG::SVFGNodeIDToNodeMapTy::iterator it = graph->begin();
        SVFG::SVFGNodeIDToNodeMapTy::iterator eit = graph->end();
        PAG *pag = graph->getPAG();
        for(PAG::iterator iter = pag->begin(); 
                        iter != pag->end(); iter++) 
        {
           //llvm::errs() << "Node: " << (*iter->second) << "\n";
           if (iter->second->hasValue() && iter->second->getValue()) 
           {
              const llvm::Instruction *inst = 
                    llvm::dyn_cast<llvm::Instruction>(iter->second->getValue());
              if (inst && inst->getOpcode() == Instruction::Store) 
              {
                 //llvm::errs() << "Node loc: " 
                 // << SVFUtil::getSourceLoc(inst) << "\n";
                 const StoreInst *si = llvm::dyn_cast<StoreInst>(inst);
                 const Value *dest = si->getPointerOperand();
                 std::set<const Instruction*> is;
                 if (pagdefs.find(dest) != pagdefs.end())
                    is = pagdefs[dest];
                 is.insert(inst);
                 pagdefs[dest] = is;
              }
           }
        }
        for (; it != eit; ++it)
        {
            //llvm:errs() << "==============\n";
            SVFGNode* node = it->second;
            const SVFGEdge::SVFGEdgeSetTy& outEdges = node->getOutEdges();
            SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = outEdges.begin();
            SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = outEdges.end();
            for (; edgeIt != edgeEit; ++edgeIt)
            {
                const SVFGEdge *edge = *edgeIt;
                VFGNode *src = dyn_cast<VFGNode>(edge->getSrcNode());
                VFGNode *dest = dyn_cast<VFGNode>(edge->getDstNode());
                assert(src && dest);
                //const ICFGNode *sicfg = src->getICFGNode();
                //IntraBlockNode* sn = SVFUtil::dyn_cast<IntraBlockNode>(sicfg);
                //const ICFGNode *dicfg = dest->getICFGNode();
                //IntraBlockNode* dn = SVFUtil::dyn_cast<IntraBlockNode>(dicfg);
                StmtVFGNode *srcst = dyn_cast<StmtVFGNode>(src);
                StmtVFGNode *destst = dyn_cast<StmtVFGNode>(dest);
                if (srcst && destst) {
                   // source loc can be found for IntraBlockNode
                   std::string ss = SVFUtil::getSourceLoc(srcst->getInst());
                   std::string ds = SVFUtil::getSourceLoc(destst->getInst());
                   if (ss != ds && ss != "" && ds != "" 
                          && ss != "empty val" && ds != "empty val")
                      llvm::errs() << "[Store-Load] source: " << ss 
                                   << " dest: " << ds << "\n";
                }
                //else if (srcst) {
                   //if (srcst->getInst()) 
                   //   llvm::errs() << (*srcst->getInst()) << "\n";
               //    std::set<SVFGNode*> visited;
               //    visitReachableUses(node, dest, visited);
               // }
                if (!srcst && destst) {
                      if (destst->getInst() && 
                               destst->getInst()->getOpcode() == 
                                                 Instruction::Store)
                      {
                
                         //llvm::errs() << "Destination store: " 
                         //<< SVFUtil::getSourceLoc(destst->getInst()) << "\n";
                         const PAGEdge* edge = destst->getPAGEdge();
                         if (edge->getEdgeKind() == PAGEdge::Store)
                         {
                            PAGNode* dpn = (PAGNode*)edge->getDstNode();  
                            //llvm::errs() << "PAG node: " << dpn << "\n";
                            std::set<SVFGNode*> ms;
                            if (mrdefs.find(dpn) != mrdefs.end())
                               ms = mrdefs[dpn];
                            ms.insert(dest);
                            mrdefs[dpn] = ms;
                         }
                      }
                   } 
                }
        }
        for(auto pdp : pagdefs)
        {
           if (pdp.second.size() > 1)
           {
              std::set<std::string> pairs;
              for(auto n1 : pdp.second) 
              {
                 for(auto n2 : pdp.second)
                 {
                    if (n1 == n2) continue;              
                    std::string s1 = SVFUtil::getSourceLoc(n1);
                    std::string s2 = SVFUtil::getSourceLoc(n2);
                    std::string pair = "";
                    if (s1 < s2)
                       pair = s1 + ":" + s2;
                    else pair = s2 + ":" + s1;
                    if (pairs.find(pair) != pairs.end())
                       continue;
                    pairs.insert(pair);
                    if (s1 != "" && s2 != "" && s1 != s2 && 
                           s1 != "empty val" && s2 != "empty val")
                       llvm::errs() << "[Store-Store]: "
                                       << s1 << "," << s2 << "\n";
                 }
              }
           }
        }
        for(auto mdp : mrdefs) 
        {
           if (mdp.second.size() > 1) 
           {
	      //llvm::errs()  << "[Store-Store] begin \n" 
              //        << "Memory region " << mdp.first
              //        << "\nNode size: " << mdp.second.size() << "\n"; 
              std::set<std::string> pairs;
              for(auto n1 : mdp.second) 
              {
                 for(auto n2 : mdp.second)
                 {
                    if (n1 == n2) continue;
                    StmtVFGNode *st1 = dyn_cast<StmtVFGNode>(n1);
                    StmtVFGNode *st2 = dyn_cast<StmtVFGNode>(n2);
                    if (st1 && st2)
                    {
                       std::string s1 = SVFUtil::getSourceLoc(st1->getInst());
                       std::string s2 = SVFUtil::getSourceLoc(st2->getInst());
                       std::string pair = "";
                       if (s1 < s2)
                          pair = s1 + ":" + s2;
                       else pair = s2 + ":" + s1;
                       if (pairs.find(pair) != pairs.end())
                          continue;
                       pairs.insert(pair);
                       if (s1 != "" && s2 != "" && s1 != s2 && 
                           s1 != "empty val" && s2 != "empty val")
                          llvm::errs() << "[Store-Store]: "
                                       << s1 << "," << s2 << "\n";
                    }
                 }
              }
              //llvm::errs()  << "[Store-Store] end \n"; 
           }
                         
        }

        /*for (; it != eit; ++it)
        {
            SVFGNode* node = it->second; 
            const SVFGEdge::SVFGEdgeSetTy& inEdges = node->getInEdges();
            SVFGEdge::SVFGEdgeSetTy::const_iterator edgeIt = inEdges.begin();
            SVFGEdge::SVFGEdgeSetTy::const_iterator edgeEit = inEdges.end();
            for (; edgeIt != edgeEit; ++edgeIt)
            {
                const SVFGEdge *edge = *edgeIt;
                VFGNode *src = dyn_cast<VFGNode>(edge->getSrcNode());
                VFGNode *dest = dyn_cast<VFGNode>(edge->getDstNode());
                assert(src && dest);
                StmtVFGNode *srcst = dyn_cast<StmtVFGNode>(src);
                StmtVFGNode *destst = dyn_cast<StmtVFGNode>(dest);
                if (srcst && destst) {
                   // source loc can be found for IntraBlockNode
                   std::string ss = SVFUtil::getSourceLoc(srcst->getInst());
                   std::string ds = SVFUtil::getSourceLoc(destst->getInst());
                   if (ss != ds)
                      llvm::errs() << "[Intra] source: " << ss 
                                   << " dest: " << ds << "\n";
                }
                else { // transitive closure  between program statements
                   llvm::errs() << "[Other]\n";
                   std::set<SVFGNode*> visited;
                   visitReachableDefs(dest, src, visited);
                }
           }
        }*/
}

void DDAStat::customStat() {
    #ifdef DBGPR 
    llvm::errs() << "SYSREL extension on custom stats\n";
    #endif
                  llvm::errs() << "instance no,"
                               << "unresolved callsite,"
                               << "callsite function," 
                               << "callsite file," 
                               << "callsite lineno," 
                               << "gep instruction," 
                               << "violation type," 
                               << "gep type," 
                               << "gep function," 
                               << "gep file," 
                               << "gep lineno\n" ;
    SVFG* graph = getSVFG();
    PAG *pag = graph->getPAG(); 
    PointerAnalysis *pta = getPTA();
    const PointerAnalysis::CallSiteToFunPtrMap& indCS = pta->getIndirectCallsites();
    PointerAnalysis::CallSiteToFunPtrMap::const_iterator csIt = indCS.begin();
    PointerAnalysis::CallSiteToFunPtrMap::const_iterator csEit = indCS.end();
    unsigned instanceno = 0;
    for (; csIt != csEit; ++csIt) {
        const CallSite& cs = csIt->first;
        if (pta->hasIndCSCallees(cs) == false) {
           instanceno++;
           unsigned neggepdep = 0;
           unsigned vargepdep = 0;
           std::set<const Instruction*> negGep, varGep;
           const Function *csfunc = cs.getInstruction()->getParent()->getParent();
           const Module *csm = csfunc->getParent();
           std::string csfilename = csm->getName();
           std::string csfuncname = csfunc->getName();
           unsigned cslineno = 0;
           llvm::MDNode *csmd = cs.getInstruction()->getMetadata("dbg");
           if (csmd) {
              llvm::DebugLoc loc(csmd);
              cslineno = loc.getLine();
           }
           llvm::errs() << instanceno 
                        << ",\"" << (*cs.getInstruction()) << "\","
                        << csfuncname << ","
                        << csfilename << ","
                        << cslineno << ","
                        << ",,,,,\n"; 
           if (const User *u = SVFUtil::dyn_cast<User>(cs.getCalledValue())) {
              getVFGDependencies(u,cs,instanceno);
           }           
           #ifdef DBGPR
           llvm::errs() << "unresolved callsite," << (*cs.getInstruction())
                        << ",negativegep=" << neggepdep 
                        << ",variablegep=" << vargepdep  
                        << ",insvfg=" << graph->hasActualINSVFGNodes(cs) 
                        << ",sourceloc=" << SVFUtil::getSourceLoc(cs.getInstruction()) << "\n";
           #endif
           
        }
    }
    /*
    PointerAnalysis::CallEdgeMap::const_iterator iter = pta->getIndCallMap().begin();
    PointerAnalysis::CallEdgeMap::const_iterator eiter = pta->getIndCallMap().end();
    for (; iter != eiter; iter++) {
	CallSite cs = iter->first;
	const PointerAnalysis::FunctionSet & functions = iter->second;
        if (functions.empty()) {
           llvm::errs() << "unresolved callsite," << (*cs.getInstruction()) << "\n";
        }
        else {
           for (PointerAnalysis::FunctionSet::const_iterator func_iter =
				functions.begin(); func_iter != functions.end(); func_iter++) {
	       const Function * callee = *func_iter;
               llvm::errs() << "callsite " << (*cs.getInstruction()) << " resolved to: " << callee->getName() << "\n";
           }
        }
    }
    SVFG::SVFGNodeIDToNodeMapTy::iterator it = graph->begin();
    SVFG::SVFGNodeIDToNodeMapTy::iterator eit = graph->end();
    for (; it != eit; ++it) {
        if (SVFUtil::isa<GepSVFGNode>(it->second)) {
            const GepSVFGNode *gepnode = SVFUtil::dyn_cast<GepSVFGNode>(it->second);
            llvm::errs() << (*gepnode) << "\n";
            NodeID src = gepnode->getPAGSrcNodeID();            
            PAGNode *srcnode = pag->getPAGNode(src);
            const Value *vs = srcnode->getValue();
            llvm::errs() << "source : " << (*vs) << "\n";  
            NodeID dst = gepnode->getPAGDstNodeID();
            PAGNode *dstnode = pag->getPAGNode(dst);
            const Value *vd = dstnode->getValue();
            llvm::errs() << "dest : " << (*vd) << "\n";	                 
            const GetElementPtrInst *gep = SVFUtil::dyn_cast<GetElementPtrInst>(vd);
            if (gep) {
               const PointsTo& srcPts = pta->getPts(src);
               bool src_ptsblackhole = false;
               bool src_fieldinsensitive = false;
               bool src_isnullptr = false;
               for (PointsTo::iterator piter = srcPts.begin(); piter != srcPts.end(); ++piter) {
                   NodeID ptd = *piter;
                   llvm::errs() << "src points to: " << ptd << "\n"; 
                   if (pag->isBlkObj(ptd)) {
                      src_ptsblackhole = true;
                   }
                   if ((flowDDA && flowDDA->isFieldInsensitive(ptd)) || 
                            (contextDDA && contextDDA->isFieldInsensitive(ptd)))
                      src_fieldinsensitive = true;
                   if (pag->isNullPtr(ptd))
                      src_isnullptr = true;
               }      
               const PointsTo& dstPts = pta->getPts(dst);
               bool dst_ptsblackhole = false;
               bool dst_fieldinsensitive = false;
               bool dst_isnullptr = false;
               for (PointsTo::iterator piter = dstPts.begin(); piter != dstPts.end(); ++piter) {
                   NodeID ptd = *piter;
                   llvm::errs() << "dst points to: " << ptd << "\n"; 
                   if (pag->isBlkObj(ptd)) {
                      dst_ptsblackhole = true;
                   }
                   if ((flowDDA && flowDDA->isFieldInsensitive(ptd)) || 
                            (contextDDA && contextDDA->isFieldInsensitive(ptd)))
                      dst_fieldinsensitive = true;
                   if (pag->isNullPtr(ptd))
                      dst_isnullptr = true;
               }  
               std::string geptype = "positive";
               const Function *func = gep->getParent()->getParent();
               const Module *m = func->getParent();
               for(User::const_op_iterator oi = gep->idx_begin(), 
                           oe = gep->idx_end(); oi != oe; oi++) {
                  Value *v = oi->get();
                  if (llvm::ConstantInt* CI = SVFUtil::dyn_cast<llvm::ConstantInt>(v)) {
                     if (CI->getSExtValue() < 0) {
                        geptype = "negative";
                     }
                  }
                  else {
                     geptype = "variable";
                  }
               }
               std::string filename = m->getName();
               std::string funcname = func->getName();
               std::string lineno = "unknown";
               int src_ptssize = srcPts.count();
               int dst_ptssize = dstPts.count();
               llvm::MDNode *md = gep->getMetadata("dbg");
               if (md) {
                  llvm::DebugLoc loc(md);
                  lineno = loc.getLine();
               }
               llvm::errs() << "gep, instr=" << (*gep) << ",type=" << geptype  
                            << ",srcptssize=" << src_ptssize 
                            << ",srcptstoblackhole=" << src_ptsblackhole 
                            << ",srcnullptr=" << src_isnullptr 
                            << ",srcfieldinsensitive=" << src_fieldinsensitive 
                            << ",dstptssize=" << dst_ptssize 
                            << ",dstptstoblackhole=" << dst_ptsblackhole 
                            << ",dstnullptr=" << dst_isnullptr 
                            << ",dstfieldinsensitive=" << dst_fieldinsensitive 
                            //<< ",offsetmatches=" << offsetmatches 
                            << ",function=" << funcname 
                            <<",file=" << filename 
                            << ",lineno=" << lineno << "\n"; 
            }
        }
    }
    */
}

