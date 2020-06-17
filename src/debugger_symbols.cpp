#include <ctype.h>

#include "debugger_symbols.h"

#include "cpu_debug.h"

void symbol_table::add(std::string name, uint32_t addr) {
    // my mingw doesn't support this    map.insert_or_assign(name, addr);
    map.erase(name);
    map.insert(std::pair<std::string, uint32_t>(name, addr));
}

bool symbol_table::find_by_addr(uint32_t addr, std::string &ret) {
    // quick and dirty lookup
    for (std::pair<std::string, uint32_t> element : map) {
        if (element.second == addr)
        {
            ret = element.first;
            return true;
        }
    }
    return false;
}


bool symbol_table::find_by_name(std::string name, uint32_t &ret) {
    std::map<std::string, uint32_t>::iterator i = map.find(name);
    if (i != map.end())
    {
        ret = i->second;
        return true;
    }
    return false;

}

void symbol_table::symbol_list(cpu_debug_t *cpu, debug_outf_t debug_outf) {
    if (length() == 0)
        debug_outf("No symbols loaded");
    for (std::pair<std::string, uint32_t> element : map) {
        char addrstr[17];
        cpu->print_addr(cpu, element.second, addrstr, 16, false);
        debug_outf("%s=%s\n", element.first.c_str(), addrstr);
    }
}

symbol_table* symbol_new() {
    return new symbol_table();
}
void symbol_free(symbol_table *symtab) {
    delete symtab;
}
void symbol_add(symbol_table *symtab, const char *name, uint32_t addr) {
    symtab->add(name, addr);
}
bool symbol_find_by_addr(symbol_table *symtab, uint32_t addr, const char **ret) {
    std::string r;
    if (symtab && symtab->find_by_addr(addr, r)) {
        char *ret2 = (char *)malloc(r.length() + 1);
        memcpy(ret2, r.c_str(), r.length() + 1);
        *ret = ret2;
        return true;
    }
    else
        return false;
}

//this will look for the first symbol (whitespace separated) in name, if a match is found *endret will point at the whitespace after 
bool symbol_find_by_name(symbol_table *symtab, const char *name, uint32_t *addr, const char**endret) {

    if (symtab == NULL)
        return false;

    const char *p = name;
    int i = 0;
    while (*p && isspace(*p))
        p++;

    while (p[i] && !isspace(p[i]))
        i++;

    if (i == 0)
        return false;

    std::string n(p, i);
    *endret = p + i;
    return symtab->find_by_name(n, *addr);
}

void symbol_list(symbol_table *symtab, cpu_debug_t *cpu, debug_outf_t debug_outf)
{
    if (!symtab)
        debug_outf("No symbols loaded");
    symtab->symbol_list(cpu, debug_outf);
}

