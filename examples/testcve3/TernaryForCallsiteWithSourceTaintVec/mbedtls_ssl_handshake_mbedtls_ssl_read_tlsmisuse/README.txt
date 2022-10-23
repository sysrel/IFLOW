IFLOW specific commandline arguments: 

  -info-flow=true : Enable IFLOW 
  -info-flow-bound=50 : Data-flow analysis depth on the dependence graph is set to 50 
  -info-flow-query-type=TernaryForCallsiteWithSourceTaintVec : Query type is TernaryForCallsiteWithSourceTaintVec
  -info-flow-sanvec-file=sanvals.txt : Santization values file when more than one value is considered 
                                       for sanitization (one integer value per line)

  -info-flow-query-ops mbedtls_ssl_handshake mbedtls_ssl_read tlsmisuse :
       The first operand is the source function (mbedtls_ssl_handshake)
       The second operand is the sink function (mbedtls_ssl_read) 
       The third operand is the descriptor that will used in the output file to refer this query (tlsmisuse)
 
Analysis results can be found in infoflowstats.txt 
 
