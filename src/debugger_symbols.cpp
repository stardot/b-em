#include <ctype.h>
#include <string.h>

#include "debugger_symbols.h"

#include "cpu_debug.h"

symbol_table::~symbol_table() {
    for (auto it = map.begin(); it != map.end(); it++) {
        delete it->second;
    }
    map.clear();
    byaddrmap.clear();
}

void symbol_table::add(const char *name, uint32_t addr) {
    auto it = map.find(name);
    symbol_entry *ent = nullptr;
    if (it != map.end()) {
        ent = it->second;
        map.erase(it);
    }
    if (ent) {
        auto i = byaddrmap.begin();
        while (i != byaddrmap.end())
            if (i->second == ent)
                byaddrmap.erase(i++);
            else
                i++;
    }
    symbol_entry *e = new symbol_entry(name, addr);
    map.insert({ e->getSymbol(), e });
    byaddrmap.insert({ e->getAddr(), e });
}

bool symbol_table::find_by_addr(uint32_t addr, const char * &ret) const {
    // quick and dirty lookup
    auto it = byaddrmap.find(addr);
    if (it != byaddrmap.end()) {
        ret = it->second->getSymbol();
        return true;
    }
    else
        return false;

}

inline uint32_t addr_distance(uint32_t a, uint32_t b)
{
    return (a <= b) ? b - a : a - b;
}

bool symbol_table::find_by_addr_near(uint32_t addr, uint32_t min, uint32_t max, uint32_t &addr_found, const char * &ret) const {

    auto it = byaddrmap.upper_bound(addr);

    if (it == byaddrmap.end())
        return false;

    bool matched = it->first >= min;
    if (matched)
    {
        ret = it->second->getSymbol();
        addr_found = it->first;
    }
    if (it != byaddrmap.begin())
    {
        it--;
        if (it->first <= max) {
            if (!matched || addr_distance(addr_found, addr) >= addr_distance(it->first, addr)) {
                ret = it->second->getSymbol();
                addr_found = it->first;
                matched = true;
            }
        }
    }

    return matched;
}

bool symbol_table::find_by_name(const char * name, uint32_t &ret) const {
    auto i = map.find(name);
    if (i != map.end())
    {
        ret = i->second->getAddr();
        return true;
    }
    return false;

}

void symbol_table::symbol_list(cpu_debug_t *cpu, debug_outf_t debug_outf) const {
    if (length() == 0)
        debug_outf("No symbols loaded");
    for (auto &element : byaddrmap) {
        char addrstr[17];
        cpu->print_addr(cpu, element.second->getAddr(), addrstr, 16, false);
        debug_outf("%s=%s\n", element.second->getSymbol(), addrstr);
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
    return symtab && symtab->find_by_addr(addr, *ret);
}

bool symbol_find_by_addr_near(symbol_table *symtab, uint32_t addr, uint32_t min, uint32_t max, uint32_t *addr_found, const char **ret) {
    return symtab && symtab->find_by_addr_near(addr, min, max, *addr_found, *ret);
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

    char *n = (char *)malloc(i + 1);
    if (!n)
        return false;
    strncpy(n, p, i);
    n[i] = '\0';
    *endret = p + i;
    return symtab->find_by_name(n, *addr);
}

void symbol_list(symbol_table *symtab, cpu_debug_t *cpu, debug_outf_t debug_outf)
{
    if (!symtab)
        debug_outf("No symbols loaded");
    symtab->symbol_list(cpu, debug_outf);
}

