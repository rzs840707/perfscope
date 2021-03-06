/**
 *  @file          Evaluator.cpp
 *
 *  @version       1.0
 *  @created       02/06/2013 12:03:39 AM
 *  @revision      $Id$
 *
 *  @author        Ryan Huang <ryanhuang@cs.ucsd.edu>
 *  @organization  University of California, San Diego
 *  
 *  Copyright (c) 2013, Ryan Huang
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *  
 *  http://www.apache.org/licenses/LICENSE-2.0
 *     
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  @section       DESCRIPTION
 *  
 *  Local risk evaluator, which is a function pass and accepts the Instruction set
 *  to be evaluated
 *
 */

#include <algorithm>
#include <iostream>
#include <queue>
#include <string>
#include <ctype.h>

#include "llvm/ADT/OwningPtr.h"
#include "llvm/IntrinsicInst.h"

#include "llvm/Support/CallSite.h"
#include "llvm/Support/raw_ostream.h"

#include "commons/handy.h"
#include "commons/CallSiteFinder.h"
#include "commons/LLVMHelper.h"
#include "analyzer/Evaluator.h"

static int INDENT = 0;

//#define EVALUATOR_DEBUG

gen_dbg(eval)

#ifdef EVALUATOR_DEBUG
gen_dbg_impl(eval)
#else
gen_dbg_nop(eval)
#endif

#ifdef EVALUATOR_DEBUG
inline void errind(int addition = 0)
{
  indent(INDENT + addition);
}
#else
inline void errind(int addition = 0)
{
}
#endif

#ifdef EVALUATOR_DEBUG
static void eval_debug(const llvm::Instruction * I)
{
  std::string str;
  llvm::raw_string_ostream OS(str);
  OS << *I;
  const char * p = str.c_str();
  while (*p && isspace(*p)) ++p;
  printf("%s", p);
}
#else
static void eval_debug(const llvm::Instruction * I)
{
}
#endif

namespace llvm {

static RiskLevel RiskMatrix[HOTNESSES][EXPNESSES] = {
  {LowRisk, LowRisk, ModerateRisk},
  {LowRisk, ModerateRisk, HighRisk},
  {ModerateRisk, HighRisk, ExtremeRisk}
};

static const char * RiskLevelStr[RISKLEVELS] = {
  "no risk",
  "low risk",
  "moderate risk",
  "high risk",
  "extreme risk"
};

static const char * HotStr[HOTNESSES] = {
  "cold",
  "regular",
  "hot"
};

static const char * ExpStr[EXPNESSES] = {
  "minor",
  "normal",
  "expensive"
};

const char * toRiskStr(RiskLevel risk)
{
  if (risk < 0 || risk > RISKLEVELS)
    return "UNKNOWN";
  return RiskLevelStr[risk];
}

const char * toHotStr(Hotness hot)
{
  if (hot < 0 || hot > HOTNESSES)
    return "UNKNOWN";
  return HotStr[hot];
}

const char * toExpStr(Expensiveness exp)
{
  if (exp < 0 || exp > EXPNESSES)
    return "UNKNOWN";
  return ExpStr[exp];
}

bool DummyLoopInfo::runOnFunction(Function &F)
{
  LoopInfo *LI = &getAnalysis<LoopInfo>(); 
  for (Function::iterator FI = F.begin() , FE = F.end(); FI != FE; ++FI) {
    BasicBlock * BB = FI;
    // Only inserted the BBs in the Loop.
    // This is to mimic the LoopInfo.getLoopFor
    if (LI->getLoopFor(BB) != NULL)
      LoopMap[&F].insert(BB); 
  }
  return false;
}

RiskLevel RiskEvaluator::assess(const Instruction *I,  std::map<Loop *, unsigned> & LoopDepthMap, Hotness FuncHotness)
{
  eval_debug(I);
  eval_debug("\n");
  errind();
  if (isa<IntrinsicInst>(I)) {
    eval_debug("intrinsic\n");
    return NoRisk;
  }
  eval_debug("expensiveness:\n");
  Expensiveness exp = calcInstExp(I);
  errind(2);
  if (exp != Expensive) {
    if (isa<BranchInst>(I)) {
      const BranchInst *BI = dyn_cast<BranchInst>(I);
      if (isPerfSensitive(BI)) {
        exp = Expensive;
        eval_debug("sensitive branch instruction\n");
      }
      else
        eval_debug("insensitive branch instruction\n");
    }
  }
  eval_debug("%s\n", toExpStr(exp)); 
  // Every modification that lies in
  // a hot function is considered hot
  if (FuncHotness == Hot)
    return RiskMatrix[Hot][exp];
  Hotness hot = Regular;
  if (!LocalLI->empty()) {
    errind();
    eval_debug("hotness:\n");
    hot = calcInstHotness(I, LoopDepthMap); 
    errind(2);
    eval_debug("%s\n", toHotStr(hot));
  }
  return RiskMatrix[hot][exp];
}

unsigned RiskEvaluator::getLoopDepth(Loop *L, std::map<Loop *, unsigned> & LoopDepthMap)
{
  std::map<Loop *, unsigned>::iterator it = LoopDepthMap.find(L);
  if (it != LoopDepthMap.end())
    return it->second;
  SmallVector<BasicBlock *, 4> exits;
  L->getExitingBlocks(exits);
  unsigned count = 0;
  for (SmallVector<BasicBlock *, 4>::iterator ei = exits.begin(), ee = exits.end();
      ei != ee; ++ei) {
    if (*ei) {
      unsigned c = SE->getSmallConstantTripCount(L, *ei); 
      if (c > count)
        count = c;
    }
  }
  LoopDepthMap[L] = count;
  return count;
}

Hotness RiskEvaluator::calcInstHotness(const Instruction *I, std::map<Loop *, unsigned> & LoopDepthMap)
{
  assert(LocalLI && SE && "Require Loop information and ScalarEvolution");
  const BasicBlock * BB = I->getParent();
  Loop * loop = LocalLI->getLoopFor(BB);
  unsigned depth = 0;
  Hotness hot = Cold;
  while (loop) {
    depth++;
    errind(2);
    unsigned cnt = getLoopDepth(loop, LoopDepthMap);
    eval_debug("L%u trip count:%u\n", depth, cnt);
    // loop count cannot be determined, so it's potentially
    // very tight!
    if (cnt == 0 || cnt > LOOPCOUNTTIGHT) 
      hot = Hot;
    else
      hot = Regular;
    loop = loop->getParentLoop();
  }
  return hot;
}

Hotness RiskEvaluator::calcFuncHotness(const char * funcName)
{
  if (profile) {
    for (Profile::iterator it = profile->begin(), ie = profile->end(); 
        it != ie; ++it) {
      if (it->first == FREQCALL) {
        if (std::binary_search(it->second.begin(), it->second.end(), funcName)) {
          errind(2);
          eval_debug("*%s*\n",toSpeFuncStr(it->first)); 
          return Hot;
        }
      }
    }
  }
  return Regular;
}

Hotness RiskEvaluator::calcFuncHotness(const Function * func)
{
  if (func)
    return calcFuncHotness(cpp_demangle(func->getName().data()));
  return Cold;
}

Hotness RiskEvaluator::calcCallerHotness(const Function * func, int level)
{
  if (func == NULL || level <= 0)
    return Cold;
  SmallPtrSet<const Function *, 4> visited;
  typedef std::pair<const Function *, int> FuncDep;
  std::queue<FuncDep> bfsQueue;
  bfsQueue.push(std::make_pair(func, 1));
  eval_debug("Callers:\n");
  unsigned callers = 0;
  while(!bfsQueue.empty()) {
    callers++;
    FuncDep & item = bfsQueue.front();
    // in case siblings with duplicates
    if (visited.count(item.first)) {
      bfsQueue.pop();
      continue;
    }
    visited.insert(item.first);
    errind(2);
    //const char *name = item.first->getName().data();
    std::string funcname(cpp_demangle(item.first->getName().data()));
    eval_debug("Depth #%d: %s ", item.second, funcname.c_str());
    Hotness hot = calcFuncHotness(funcname.c_str());
    eval_debug("%s\n", toHotStr(hot));
    if (hot == Hot) {
      return Hot;
    }
    // Don't trace upward too much
    if (item.second >= level) {
      bfsQueue.pop();
      continue;
    }
    CallSiteFinder csf(item.first);
    CallSiteFinder::cs_iterator ci = csf.begin(), ce = csf.end();
    if(ci == ce) { 
      errind(4);
      eval_debug("no caller\n"); 
      bfsQueue.pop();
      continue;
    }
    // Push to the queue and increment the depth
    for (; ci != ce; ++ci) {
      const Function *caller = ci->first;
      if (visited.count(caller))
        continue;
      //TODO test if CallInst is in loop
      if (!caller->isDeclaration() && ci->second) {
        if (!loop_analyzed.count(caller)) {
          if (func_manager) {
            func_manager->run(*(const_cast<Function *>(caller)));
            loop_analyzed.insert(caller);
          }
        }
        errind(4);
        eval_debug("called from %s [", cpp_demangle(caller->getName().data()));
        eval_debug(ci->second);
        eval_debug("], ");
        const BasicBlock * BB = ci->second->getParent();
        if (GlobalLI->inLoop(caller, BB)) {
          eval_debug("in loop\n");
          return Hot;
        }
        eval_debug("not in loop\n"); 
      }
      bfsQueue.push(std::make_pair(caller, item.second + 1));
    }
    bfsQueue.pop();
  }
  if (callers > CALLERHOT)
    return Hot;
  //TODO define cold function
  return Regular;
}

Expensiveness RiskEvaluator::calcInstExp(const Instruction * I)
{
  Expensiveness exp = Minor;
  if (const CallInst * CI = dyn_cast<CallInst>(I)) {
//  if (isa<CallInst>(I) || isa<InvokeInst>(I)) {
    const Value * called = CI->getCalledValue();
    if (const Function *F = dyn_cast<Function>(called)) { 
      if (!F->isIntrinsic())
        return calcFuncExp(F);
    }
    return exp;
  }
  if(cost_model) {
    unsigned cost = cost_model->getInstructionCost(I); 
    errind(2);
    eval_debug("cost: %u\n", cost);
    if (cost == 0 || cost == (unsigned) -1)
      exp = Minor;
    else
      if (cost > INSTEXP)
        exp = Expensive;
      else
        exp = Normal;
  }
  return exp;
}

Expensiveness RiskEvaluator::calcFuncExp(const char * funcName)
{
  if (profile) {
    for (Profile::iterator it = profile->begin(), ie = profile->end(); 
        it != ie; ++it) {
      if (it->first == SYSCALL || it->first == LOCKCALL || it->first == EXPCALL) {
        if (std::binary_search(it->second.begin(), it->second.end(), funcName)) {
          errind(2);
          eval_debug("*%s*\n",toSpeFuncStr(it->first)); 
          return Expensive;
        }
      }
    }
  }
  return Normal;
}

Expensiveness RiskEvaluator::calcFuncExp(const Function * func)
{
  if (func)
    return calcFuncExp(cpp_demangle(func->getName().data()));
  return Minor;
}

bool RiskEvaluator::isPerfSensitive(const BranchInst *I)
{
  // Unconditional branch is not performance sensitive
  if (I->isUnconditional())
    return false;
  unsigned exps = 0;
  unsigned succs = I->getNumSuccessors();
  for (unsigned i = 0; i < succs; ++i) {
    BasicBlock * BB = I->getSuccessor(i);
    for (BasicBlock::iterator BI = BB->begin(), BE = BB->end(); BI != BE; BI++) {
      if (calcInstExp(BI) == Expensive)
        exps++;
    }
  }
  return exps != 0 && exps != succs;
}


bool RiskEvaluator::runOnFunction(Function &F)
{
  if (!m_inst_map.count(&F)) {
    errs() << F.getName() << " not in the target\n";
    return false;
  }
  memset(FuncRiskStat, 0, sizeof(FuncRiskStat));
  LocalLI = &getAnalysis<LoopInfo>(); 
  SE = &getAnalysis<ScalarEvolution>(); 
  INDENT = 4;
  InstVecTy &inst_vec = m_inst_map[&F];
  std::map<Loop *, unsigned> LoopDepthMap;
#if 0
  DepGraph * graph = NULL;
  DepGraphBuilder * builder = NULL; // don't use OwnigPtr;
  OwningPtr<FunctionPassManager> FPasses;
#endif
  Hotness funcHot = calcCallerHotness(&F, depth);
#if 0
  if (level > 1) {
    // TODO add DepGraphBuilder on the fly
    FPasses.reset(new FunctionPassManager(module));
    builder = new DepGraphBuilder();
    FPasses->add(builder);
    FPasses->doInitialization();
    FPasses->run(F);
    //DepGraphBuilder & builder = getAnalysis<DepGraphBuilder>();
    graph = builder->getDepGraph();
  }
#endif
  if (level > 1) {
    InstVecIter I = inst_vec.begin(), E = inst_vec.end();
    slicer->addCriteria(&F, I, E);
    slicer->computeSlice();
  }
  for (InstVecIter I = inst_vec.begin(), E = inst_vec.end(); I != E; I++) {
    Instruction* inst = *I;
    RiskLevel max = assess(inst, LoopDepthMap, funcHot);
    errind();
    eval_debug("%s\n", toRiskStr(max));

    if (level > 1) {
      const Instruction * propagate;
      eval_debug("Evaluating slice...\n");
      while ((propagate = slicer->next()) != NULL) {
        RiskLevel r = assess(propagate, LoopDepthMap, funcHot);
        //We could break once we reach ExtremeRisk
        //But we just iterate all over it for now
        if (r > max)
          max = r;
        errind();
        eval_debug("%s\n", toRiskStr(r));
      }
      eval_debug("Slice evaluation done.\n");
    }
#if 0
    if (graph != NULL) {
      Slicer slicer(graph, Criterion(0, inst, true, AllDep));
      Instruction * propagate;
      eval_debug("Evaluating slice...\n");
      while ((propagate = slicer.next()) != NULL) {
        RiskLevel r = assess(propagate, LoopDepthMap, funcHot);
        //We could break once we reach ExtremeRisk
        //But we just iterate all over it for now
        if (r > max)
          max = r;
        errind();
        eval_debug("%s\n", toRiskStr(r));
      }
      eval_debug("Slice evaluation done.\n");
    }
#endif

    //We only count slice once, otherwise the output doesn't make
    //much sense.
    FuncRiskStat[max]++;
    AllRiskStat[max]++;
  }
  statFuncRisk(cpp_demangle(F.getName().data()));
#if 0
  if (level > 1) {
    FPasses->doFinalization();
  }
#endif
  return false;
}

inline void RiskEvaluator::statPrint(unsigned stat[RISKLEVELS])
{
  for (int i = 0; i < RISKLEVELS; i++) {
    printf("%s:\t%u\n", toRiskStr((RiskLevel) i), stat[i]);
  }
}

void RiskEvaluator::statFuncRisk(const char * funcname)
{
  printf("===='%s' risk summary====\n", funcname);
  statPrint(FuncRiskStat);
}

void RiskEvaluator::statAllRisk()
{
  printf("====Overall risk summary====\n");
  statPrint(AllRiskStat);
}


char RiskEvaluator::ID = 0;
char DummyLoopInfo::ID = 0;
const char * RiskEvaluator::PassName = "Risk evaluator pass";
const char * DummyLoopInfo::PassName = "Dummy Loop Info";
} // End of llvm namespace

