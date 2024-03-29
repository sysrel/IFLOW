/*
 * DDAStat.h
 *
 *  Created on: Sep 15, 2014
 *      Author: Yulei Sui
 */

#ifndef DDASTAT_H_
#define DDASTAT_H_

#include "Util/PTAStat.h"

class FlowDDA;
class ContextDDA;
class SVFG;
class PointerAnalysis;
class PAGNode;

/*!
 * Statistics of demand-driven analysis
 */
class DDAStat : public PTAStat {

public:
    DDAStat(FlowDDA* pta);
    DDAStat(ContextDDA* pta);

    u32_t _NumOfDPM;
    u32_t _NumOfStrongUpdates;
    u32_t _NumOfMustAliases;
    u32_t _NumOfInfeasiblePath;

    u64_t _NumOfStep;
    u64_t _NumOfStepInCycle;
    double _AnaTimePerQuery;
    double _AnaTimeCyclePerQuery;
    double _TotalTimeOfQueries;
    double _TotalTimeOfBKCondition;

    NodeBS _StrongUpdateStores;

    void performStatPerQuery(NodeID ptr);

    void performStat();

    void printStat();

    void printStatPerQuery(NodeID ptr, const PointsTo& pts);

    void getNumOfOOBQuery();

    inline void setMemUsageBefore(u32_t vmrss, u32_t vmsize) {
        _vmrssUsageBefore = vmrss;
        _vmsizeUsageBefore = vmsize;
    }

    inline void setMemUsageAfter(u32_t vmrss, u32_t vmsize) {
        _vmrssUsageAfter = vmrss;
        _vmsizeUsageAfter = vmsize;
    }

private:
    FlowDDA* flowDDA;
    ContextDDA* contextDDA;

    u32_t _TotalNumOfQuery;
    u32_t _TotalNumOfOutOfBudgetQuery;
    u32_t _TotalNumOfDPM;
    u32_t _TotalNumOfStrongUpdates;
    u32_t _TotalNumOfMustAliases;
    u32_t _TotalNumOfInfeasiblePath;

    u32_t _TotalNumOfStep;
    u32_t _TotalNumOfStepInCycle;

    u32_t _NumOfIndCallEdgeSolved;
    u32_t _MaxCPtsSize;
    u32_t _MaxPtsSize;
    u32_t _TotalCPtsSize;
    u32_t _TotalPtsSize;
    u32_t _NumOfNullPtr;
    u32_t _NumOfConstantPtr;
    u32_t _NumOfBlackholePtr;

    u32_t _vmrssUsageBefore;
    u32_t _vmrssUsageAfter;
    u32_t _vmsizeUsageBefore;
    u32_t _vmsizeUsageAfter;

    double _AvgNumOfDPMAtSVFGNode;
    u32_t _MaxNumOfDPMAtSVFGNode;

    NUMStatMap NumPerQueryStatMap;

    void initDefault();

public:
    SVFG* getSVFG() const;

    PointerAnalysis* getPTA() const;

    inline NodeBS& getStrongUpdateStores() {
        return _StrongUpdateStores;
    }

    /* SYSREL extension */
    void learnCandidateTargetResolutions();
    void findReachingSendWithoutReachingRecv();
    void customStat();
    void featureExtraction();
    void findPatterns();
    void performInfoFlow();
    void getDependencies(User*, std::set<const Instruction*>&, std::set<const Instruction*>);
    void getVFGDependencies(const User*, const CallSite &cs, unsigned instanceno);
    void printPtsInfo(PAGNode *pn);
    bool violatesSoundness(NodeID q, const CallSite &cs, unsigned instanceno);
    /* SYSREL extension end */
};


#endif /* DDASTAT_H_ */
