#include "Debug.hh"
#include "Error.hh"
#include "Liberty.hh"
#include "Network.hh"
#include "PortDirection.hh"
#include "Report.hh"
#include "Stats.hh"
#include "VerilogNamespace.hh"
#include "verilog/VerilogReaderPvt.hh"
#include "verilog/NameMapping.hh"

namespace sta {
namespace NameResolve {

// ast access methods
Module::Range
Module::getRange(VerilogDclBus* bus) {
  int from = bus->fromIndex();
  int to = bus->toIndex();
  return Range(from, to);
}
bool 
Module::portdir(std::string const & port) const {
  return module->declaration(port.c_str())->direction()->isInput();
}

#define ADDNETANDCONN(instport, connexpr, isInput)    \
  addNetSymbol(instport, false);                      \
  addNetSymbol(connexpr, false);                      \
  if (isInput) { addConnection(connexpr, instport); } \
  else         { addConnection(instport, connexpr); } 

void
Module::connectPin(std::string const & instname, VerilogNetPortRefScalarNet *pin, Module* submod) {
  std::string subport = pin->name();
  bool isInput = submod->portdir(subport);
  std::string instport = instname + '/' + subport;
  std::string connexpr = pin->netName();
  VerilogDcl *subportDecl = submod->module->declaration(subport.c_str());
  if (!subportDecl->isBus()) {
    ADDNETANDCONN(instport, connexpr, isInput);
  } else {
    VerilogDclBus *portDecl = (VerilogDclBus*)subportDecl;
    Range portRange = getRange(portDecl);
    addBusSymbol(instport, false, portRange.left, portRange.right);

    VerilogDclBus *netDecl  = (VerilogDclBus*)module->declaration(connexpr.c_str());
    if (netDecl) {
      Range netRange = getRange(netDecl);
      addBusSymbol(connexpr, false, netRange.left,  netRange.right);
      while (portRange.hasNext()) {
        std::stringstream ssp, ssn;
        ssp << instport << "[" << portRange.curpos << "]";
        ssn << connexpr << "[" << netRange.curpos << "]";
        portRange.incrpos();
        netRange.incrpos();
        ADDNETANDCONN(ssp.str(), ssn.str(), isInput);
      }
    } else {
      addNetSymbol(connexpr, false);
      std::stringstream ssp;
      ssp << instport << "[" << portRange.curpos << "]";
      ADDNETANDCONN(ssp.str(), connexpr, isInput);
    }
  }
}

void
Module::connectBus(std::string const & instname, VerilogNetPortRef *bus, Module* submod, Cell *cell) {
  //  auto &pin_it = ml->reader
  std::string subport = bus->name();
  bool isInput = submod->portdir(subport);

  VerilogNetNameIterator *netIt = bus->nameIterator(submod->module, ml->reader);
  Port *port = ml->network->findPort(cell, subport.c_str());
  if (!ml->network->hasMembers(port)) {
    std::string netname  = netIt->next();
    std::string portname = instname + '/' + ml->network->name(port);
    ADDNETANDCONN(portname, netname, isInput);
    return;
  }
  PortMemberIterator *portIt = ml->network->memberIterator(port);
  while(netIt->hasNext()){
    std::string netname  = netIt->next();
    std::string portname = instname + '/' + ml->network->name(portIt->next());
    ADDNETANDCONN(portname, netname, isInput);
  }
}

#undef ADDNETANDCONN

void 
Module::processModuleInst(VerilogModuleInst* s) {
  std::string instname = s->instanceName();
  std::string submodname  = s->moduleName();
  addInstSymbol(instname, submodname);
  Cell *cell = ml->network->findAnyCell(submodname.c_str());
  VerilogModule* cellmod = ml->reader->module(cell);
  Module *submod = ml->createModule(submodname, cellmod);
  for (auto & s : submod->symbols) {
    if (!s.second.isPort) continue;
    std::string portname = instname + '/' + s.first;
    if (!s.second.isBus()) { 
      addNetSymbol(portname, false);
    } else {
      addBusSymbol(portname, false, s.second.range->left, s.second.range->right);
    }
  }
  VerilogNetSeq *netSeq = s->pins();
  if (netSeq) {
    for (auto & pin: *netSeq) {
      if (pin->isNamedPortRefScalarNet()) {
        connectPin(instname, (VerilogNetPortRefScalarNet*)pin, submod);
        continue;
      }
      if (pin->isNamedPortRef()) {
        connectBus(instname, (VerilogNetPortRef*)pin, submod, cell);
      }
    }
  }
}

void
Module::processLibertyInst(VerilogLibertyInst* s) {
  const char  *instname = s->instanceName();
  const char **netNames = s->netNames();
  LibertyCell *cell = s->cell();
  addInstSymbol(instname, "");
  auto iter = cell->portIterator();
  while (iter->hasNext()) {
    auto item = iter->next();
    const char *port = item->name();
    if (netNames[item->pinIndex()]) {
      const char *pin = netNames[item->pinIndex()];
      std::string instport = std::string(instname) + "/" + port;
      addNetSymbol(instport, false);
      addNetSymbol(pin, false);
      if (item->direction()->isInput()) addConnection(pin, instport);
      else                              addConnection(instport, pin);
    }
  }
}

void
Module::processDeclaration(VerilogDcl* dcl) {
  std::string portName = dcl->portName();
  bool isPort = !dcl->direction()->isInternal();
  if (!dcl->isBus()) return addNetSymbol(portName, isPort);
  Range r = getRange((VerilogDclBus*)dcl);
  addBusSymbol(portName, isPort, r.left, r.right);
}

#define ADDNETASSIGN(lhs, rhs)    \
  addNetSymbol(lhs, false);       \
  addNetSymbol(rhs, false);       \
  addConnection(rhs, lhs);

void 
Module::processAssign(VerilogAssign* s) {
  VerilogNet* l = s->lhs();
  VerilogNet* r = s->rhs();
  std::string lnetname = l->name();
  std::string rnetname = r->name();
  VerilogDcl* ldcl = module->declaration(lnetname.c_str());
  VerilogDcl* rdcl = module->declaration(rnetname.c_str());
  // both side is bus
  if (ldcl && rdcl && ldcl->isBus() && rdcl->isBus()) {
    Range lr = getRange((VerilogDclBus*)ldcl);
    Range rr = getRange((VerilogDclBus*)rdcl);
    while (lr.hasNext()) {
      std::stringstream ssl, ssr;
      ssl << lnetname << "[" << lr.curpos << "]";
      ssr << rnetname << "[" << rr.curpos << "]";
      lr.incrpos();
      rr.incrpos();
      ADDNETASSIGN(ssl.str(), ssr.str());
    }
  } else {
    //only one side of assign is bus, just split the bus side
    if (ldcl && ldcl->isBus()) {
      Range r = getRange((VerilogDclBus*)ldcl);
      std::stringstream ss;
      ss << lnetname << "[" << r.curpos << "]";
      lnetname = ss.str();
    }
    if (rdcl && rdcl->isBus()) {
      Range r = getRange((VerilogDclBus*)rdcl);
      std::stringstream ss;
      ss << rnetname << "[" << r.curpos << "]";
      rnetname = ss.str();
    }
    ADDNETASSIGN(lnetname, rnetname);
  }
}
#undef ADDNETASSIGN
void
Module::processStmt(VerilogStmt* s) {
  if (s->isModuleInst())  { return processModuleInst((VerilogModuleInst*)s); }
  if (s->isLibertyInst()) { return processLibertyInst((VerilogLibertyInst*)s); }
  if (s->isDeclaration()) { return processDeclaration((VerilogDcl*)s); }
  if (s->isAssign())      { return processAssign((VerilogAssign*)s); }
  assert(0);
}

void
Module::processModule() {
  for (auto &s : *module->ports()) {
    VerilogDcl* dcl = module->declaration(s->name());
    if (!dcl->isBus()) addNetSymbol(s->name(), true); 
    else {
      Range r = getRange((VerilogDclBus*)dcl);
      addBusSymbol(s->name(), true, r.left, r.right);
    }
  }
  for (auto &s : *module->stmts()) { processStmt(s); }
}

// fill methods
void 
Module::addInstSymbol(std::string const &instname, std::string const &modname) {
  Symbol &sym = addSymbol(instname);
  sym.isInst     = true;
  sym.isNet      = false;
  sym.name       = instname;
  sym.moduleName = modname;
}
void
Module::addNetSymbol(std::string const &netname, bool isPort) {
  Symbol &sym = addSymbol(netname);
  sym.isNet  = true;
  sym.isInst = false;
  sym.name   = netname;
  sym.isPort = isPort;
}
void
Module::addBusSymbol(std::string const &netname, bool isPort, int left, int right) {
  if (symbols.count(netname)) return;
  Symbol &sym = addSymbol(netname);
  sym.isNet  = true;
  sym.isInst = false;
  sym.name   = netname;
  sym.isPort = isPort;
  sym.range  = new Range(left, right);
}

// search methods
Module*
Module::instModule(std::string const & instname) const {
  std::string modname = symbols.find(instname)->second.moduleName;
  return modname == "" ? NULL : ml->getModule(modname);
}

Module::StringVec
Module::findHierSource(std::string const & key) {
  size_t pos = key.find('/');
  if (pos == -1) return findSource(key);
  std::string instname = key.substr(0, pos);
  std::string hiername = key.substr(pos + 1);
  Module* instmod = instModule(instname);
  if (!instmod) return StringVec(1, key);
  StringVec subress = instmod->findHierSource(hiername);
  StringVec finalRes;
  for (auto & subres : subress) {
    std::string res = instname + "/" + subres;
    finalRes.push_back(findOneSource(res));
  }
  return finalRes;
}

Module::StringVec
Module::findSource(std::string const & key) {
  Symbols::iterator it = symbols.find(key);
  if (it == symbols.end()) return StringVec(1, key);
  if (!it->second.isBus()) return StringVec(1, findOneSource(key));
  StringVec res;
  Range& range = *(it->second.range);
  range.resetpos();
  while (range.hasNext()) {
    std::stringstream ss; ss << key << "[" << range.curpos << "]";
    res.push_back(findOneSource(ss.str()));
    range.incrpos();
  }
  return res;
}

std::string 
Module::findOneSource(std::string const & key) {
  Symbols::iterator it = symbols.find(key);
  // if key is not in table
  if (it == symbols.end()) return key;
  assert(!it->second.isBus());
  // if (key not in current module step in)
  std::string res = it->second.src;
  if (res == key) return res;
  if (res == "") {
    size_t pos = key.find('/');
    if (pos == -1) return key;  
    std::string instname = key.substr(0, pos);
    Module* instmod = instModule(instname);
    if (!instmod) return key;
    assert(key != key.substr(pos + 1));
    res = instname + "/" + instmod->findOneSource(key.substr(pos + 1));
    it->second.src = res;
  }
  res = findOneSource(res);
  it->second.src = res;  // update table
  return res;
};

Module*
ModuleList::createModule(std::string const &name, VerilogModule* m) {
  Modules::iterator it = modules.find(name);
  if (it != modules.end()) return it->second;
  Module* mod = new Module(m, (ModuleList*)this);
  return modules.insert({name, mod}).first->second;
}

} // end namespace NameResolve
} // end namespace sta