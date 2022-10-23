IFLOW specific commandline arguments: 

  -info-flow=true : Enable IFLOW 
  -info-flow-bound=20 : Data-flow analysis depth on the dependence graph is set to 20 
  -info-flow-query-type=TaintsPtrArgDirectCallToPrimValArg : Query type is TaintsPtrArgDirectCallToPrimValArg
  -info-flow-query-ops receive memcpy taintedlen

      The first operand is the source entity (some pointer arg in any callsite of function receive) 
      The second operand is the sink function (memcpy) whose primitive type argument will be used as a sink 
      The third operand is the descriptor that will used in the output file to refer this query (taintedlen)

Analysis results can be found in infoflowstats.txt
