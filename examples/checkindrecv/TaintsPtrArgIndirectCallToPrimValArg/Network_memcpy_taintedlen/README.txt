
IFLOW specific commandline arguments:

 -info-flow=true : Enable IFLOW

 -info-flow-bound=50 : Data-flow analysis depth on the dependence graph is set to 50

 -info-flow-query-type=TaintsPtrArgIndirectCallToPrimValArg : Query type is TaintsPtrArgIndirectCallToPrimValArg

 -info-flow-query-ops Network memcpy taintedlen : 
     
         The first operand is the source entity (any callsite of function pointers defined in Network)
         The second operand is the sink function (memcpy)
         The third operand is the descriptor that will used in the output file to refer this query (taintedlen)

Analysis results can be found in infoflowstats.txt 
