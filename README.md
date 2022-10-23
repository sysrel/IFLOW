<p>IFLOW is a tool that is built on top of SVF, which enables scalable and precise interprocedural dependence analysis 
for C and C++ programs. 
IFLOW allows users to write queries that refer to fine-grain code elements such as function parameters of 
certain type when defining sources and sinks for data-flow analysis. Also, IFLOW provides control-flow sanitization to filter 
data-flow paths that use certain values of API return values. So, IFLOW is suitable for scalable API rule conformance 
checking and has been applied to IoT Framework APIs. If you se IFLOW, please cite our paper:
</p>

```  
@inproceedings{YB22,
  author    = {Tuba Yavuz and
               Christopher Brant},
  editor    = {Anupam Joshi and
               Maribel Fern{\'{a}}ndez and
               Rakesh M. Verma},
  title     = {Security Analysis of IoT Frameworks using Static Taint Analysis},
  booktitle = {{CODASPY} '22: Twelveth {ACM} Conference on Data and Application Security
               and Privacy, Baltimore, MD, USA, April 24 - 27, 2022},
  pages     = {203--213},
  publisher = {{ACM}},
  year      = {2022},
}
```  

We recommend you to build IFLOW using cmake by consulting the 
![setup document for SVF:](https://github.com/svf-tools/SVF/wiki/Setup-Guide#getting-started)

IFLOW implements a set of query types. In this version, the queries are specified on the commandline (We will push a version 
with a grammar-based input file later). Please check out the examples directory for more information. 
The real-world IoT framework benchmarks are located under the benchmarks directory.
