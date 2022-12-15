#include <cassert>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include "VerilogReaderPvt.hh"

namespace sta {
class VerilogModule;

namespace NameResolve {
class ModuleList;

class Module {
public:
  enum {
    PORT_INPUT = 0,
    PORT_OUTPUT
  };
  struct Range {
    Range() : left(0), right(0), isIncr(false), curpos(0) {}
    Range(int f, int t) : left(f), right(t), isIncr(right > left), curpos(left) {}
    int  left;
    int  right;
    bool isIncr;
    // iteration function
    int curpos;
    void incrpos() { curpos += isIncr ? 1 : -1; }
    void resetpos() { curpos = left; }
    bool hasNext() const { return isIncr ? curpos <= right : curpos >= right; }
  };
  // build symbol table
  struct Symbol {
    Symbol():isInst(false), isNet(false), isPort(false), range(NULL) {}
    ~Symbol() { if (range) delete range; }
    bool isInst : 1;
    bool isNet : 1;
    bool isPort : 1;
    std::string name;
    std::string moduleName;
    std::string src;
    Range*      range;
    bool isBus() const { return range; }
  };

  // ast access methods
  Range getRange(VerilogDclBus* bus);
  bool portdir(std::string const & port) const;
  void connectPin(std::string const & instname, VerilogNetPortRefScalarNet *pin, Module* submod); 
  void connectBus(std::string const & instname, VerilogNetPortRef *bus, Module* submod, Cell *cell);

  void processModuleInst(VerilogModuleInst* s);
  void processLibertyInst(VerilogLibertyInst* s);
  void processLibertyInstAsModule(VerilogModuleInst* s);
  void processDeclaration(VerilogDcl* dcl);
  void processAssign(VerilogAssign* s);
  void processStmt(VerilogStmt* s);
  void processModule();

  void addInstSymbol(std::string const &instname, std::string const &modname);
  void addNetSymbol(std::string const &netname, bool isPort);
  void addBusSymbol(std::string const &netname, bool isPort, int left, int right);
  void addConnection(std::string const &from, std::string const &to) {
	 std::string f = nameRegulation(from);
	 std::string t = nameRegulation(to);
    assert(symbols.count(t));
    symbols.find(t)->second.src = f;
  }
  void connectBufferPins(LibertyCell* cell, std::string const & instname);
  // search methods
  typedef std::vector<std::string> StringVec;
  StringVec findHierSource(std::string key);
  StringVec findSource(std::string const & key);
  std::string findOneSource(std::string const & key);

  // constructor
  Module(VerilogModule* m, ModuleList* l) : module(m), ml(l) {
    processModule();
  }
private:
  std::string nameRegulation(std::string const & name) const;
  Module* instModule(std::string const & instname) const;
  Symbol &addSymbol(std::string const &name) {
	 std::string n = nameRegulation(name);
    return symbols.insert({n, Symbol()}).first->second;
  }
  typedef std::unordered_map<std::string, Symbol> Symbols;
  Symbols        symbols;
  VerilogModule *module;
  ModuleList    *ml;

public:
  typedef typename Symbols::const_iterator const_iterator;
  typedef typename Symbols::key_type key_type;
  typedef typename Symbols::value_type value_type;
  void print() const;
};

class ModuleList {
public:
  Module *createModule(std::string const &name, VerilogModule* m);
  ~ModuleList() {
    for (auto &x : modules)
      delete x.second;
  }
  void print();
  
  void
  printRes(std::string const &path) const {
      Module::StringVec res = findSource(path);
      while(!res.empty()) {
        std::string tmp = res.back();
        res.pop_back(); 
        std::cout << path << " ----> " << tmp << std::endl;
      }
  }
  Module::StringVec
  findSource(std::string const &path) const {
    Module* root = getModule(rootModule);
    Module::StringVec res = root->findHierSource(path);
    if (!res.size()) res.push_back(path);
    return res;
  }
  Module *getModule(std::string modname) const {
    return modules.find(modname)->second;
  }

  ModuleList(std::string const & rmName, NetworkReader* nl, VerilogReader* rd) : rootModule(rmName), network(nl), reader(rd) {}
private:
  typedef std::unordered_map<std::string, Module *> Modules;
  Modules        modules;
public:
  std::string    rootModule;
  NetworkReader* network;
  VerilogReader* reader;
};

} // end namespace NameResolve
} // end namespace sta
