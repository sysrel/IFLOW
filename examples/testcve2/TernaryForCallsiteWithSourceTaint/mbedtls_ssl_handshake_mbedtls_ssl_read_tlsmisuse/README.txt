IFLOW specific commandline arguments in run.sh: 

  -info-flow=true : Enable IFLOW 
  -info-flow-bound=50 : Data-flow analysis depth on the dependence graph is set to 50 
  -info-flow-query-type=TernaryForCallsiteWithSourceTaint : Query type is TaintsPtrArgIndirectCallToPrimValArg
  -info-flow-query-ops mbedtls_ssl_handshake mbedtls_ssl_read tlsmisuse 2 
      The first operand is the source entity (any callsite of function mbedtls_ssl_handshake) 
      The second operand is the sink function (mbedtls_ssl_read tlsmisuse) 
      The third operand is the descriptor that will used in the output file to refer this query (tlsmisuse) 
      The fourth operand is the sanitization value (2)

Analysis results can be found in infoflowstats.txt 
