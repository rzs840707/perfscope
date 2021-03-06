//===---- Matcher.h - Match Scope To IR Elements----*- C++ -*-===//
//
//===----------------------------------------------------------------------===//
#ifndef ___MATCHER__H_
#define ___MATCHER__H_

#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/ADT/Statistic.h"

#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Metadata.h"
#include "llvm/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/Dwarf.h"
#include "llvm/Support/CFG.h"
#include "llvm/Constants.h"
#include "llvm/Instruction.h"
#include "llvm/InstrTypes.h"

#include <vector>
#include <deque>

#include "commons/Scope.h"

using namespace llvm;

typedef DomTreeNodeBase<BasicBlock> BBNode;

template <class T1, class T2> struct Pair
{
  typedef T1 first_type;
  typedef T2 second_type;

  T1 first;
  T2 second;

  Pair() : first(T1()), second(T2()) {}
  Pair(const T1& x, const T2& y) : first(x), second(y) {}
  template <class U, class V> Pair (const Pair<U,V> &p) : first(p.first), second(p.second) { }
};

typedef struct MetadataElement {
  unsigned key;
  DIDescriptor value;
} MetadataElement;

typedef struct MetadataNode {
  MetadataNode *parent;
  MetadataNode *left;
  MetadataNode *right;
  MetadataElement Element;
} MetadataNode;

class MetadataTree {
  public:
    MetadataNode *root;
    void insert(MetadataNode *);
    MetadataNode *search(unsigned key);
};

typedef Pair<DISubprogram, int> DISPExt;

class DISPCopy {
  public:
    std::string directory;
    std::string filename;
    std::string name;

    unsigned linenumber;
    unsigned lastline;
    Function *function;

  public:
    DISPCopy(DISubprogram & DISP)
    {
      filename = DISP.getFilename();
      //TODO
      // Only copy directory if filename doesn't contain
      // path information.
      // If source code is compiled in another directory,
      // filename in DU will look like:
      // /path/to/src/file
      // if (!(filename.size() > 0 && filename.at(0) == '/'))
      directory = DISP.getDirectory();
      name = DISP.getName();
      linenumber = DISP.getLineNumber();
      lastline = 0;
      function = DISP.getFunction();
    }
};

bool cmpDISP(const DISubprogram &, const DISubprogram &);
bool cmpDISPCopy(const DISPCopy &, const DISPCopy &);

bool skipFunction(Function *);


class ScopeInfoFinder {
  public:
    static unsigned getInstLine(const Instruction *);
    static unsigned getLastLine(Function *);
    static bool getBlockScope(Scope & , BasicBlock *);
    static bool getLoopScope(Scope & , Loop *);


};

class Matcher {
  public:
    typedef std::vector<DISPCopy>::iterator sp_iterator;
    typedef std::vector<DICompileUnit>::iterator cu_iterator;

  protected:
    bool initialized;
    bool processed;
    std::string filename;
    const char *patchname;
    Module & module;

  public:
    std::vector<DISPCopy> MySPs;
    std::vector<DICompileUnit> MyCUs;

    int patchstrips;
    int debugstrips;

  public:
    Matcher(Module &M, int d_strips = 0, int p_strips = 0) : module(M)
    {
      patchstrips = p_strips; 
      debugstrips = d_strips; 
      initialized = false;
      processCompileUnits(M); 
      processed = true;
    }
    //Matcher() {initialized = false; processed = false; patchstrips = 0; debugstrips = 0; }

    void processCompileUnits(Module &);

    void processSubprograms(Module &);
    void processSubprograms(DICompileUnit &);
    void processInst(Function *);
    void processBasicBlock(Function *);
    void processLoops(LoopInfo &);
    void processDomTree(DominatorTree &);

    cu_iterator matchCompileUnit(StringRef);
    Function * matchFunction(sp_iterator &, Scope &, bool &);
    Instruction * matchInstruction(inst_iterator &, Function *, Scope &);
    static Loop * matchLoop(LoopInfo &li, const Scope &);


    void process(Module &M) 
    { 
      processCompileUnits(M); 
      processSubprograms(M); 
      processed = true; 
    } 

    void setstrips(int p_strips, int d_strips) 
    {
      patchstrips = p_strips; 
      debugstrips = d_strips; 
    }

    sp_iterator resetTarget(StringRef);

    sp_iterator slideSPToTarget(StringRef);
    sp_iterator initMatch(cu_iterator &);

    inline sp_iterator sp_begin() { return MySPs.begin(); }
    inline sp_iterator sp_end() { return MySPs.end(); }

    inline cu_iterator cu_begin() { return MyCUs.begin(); }
    inline cu_iterator cu_end() { return MyCUs.end(); }


    void preTraversal(Function *);
    void succTraversal(Function *);

  protected:
    Function * __matchFunction(sp_iterator, Scope &);
    bool initName(StringRef);
    void dumpSPs();

};

#endif
