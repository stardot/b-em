#include <ctype.h>

#include "debugger_symbols.h"

#include "cpu_debug.h"

void symbol_table::add(const char *name, uint32_t addr) {
    auto it = map.find(name);
    if (it != map.end()) {
        map.erase(it);
    }
    symbol_entry e(name, addr);
    map.insert({ e.getSymbol(), std::move(e) });
}

bool symbol_table::find_by_addr(uint32_t addr, const char * &ret) {
    // quick and dirty lookup
    for (auto &element : map) {
        if (element.second.getAddr() == addr)
        {
            ret = element.second.getSymbol();
            return true;
        }
    }
    return false;
}

bool symbol_table::find_by_addr_near(uint32_t addr, uint32_t min, uint32_t max, uint32_t *addr_found, const char *&ret) {

    bool matched = false;
    uint32_t distance = 0;

    for (auto &element : map) {
        if (element.second.getAddr() >= min && element.second.getAddr() <= max)
        {
            if (matched)
            {
                uint32_t ndistance = (element.second.getAddr() <= addr) ? addr - element.second.getAddr() : element.second.getAddr() - addr;
                if (ndistance < distance) {
                    ret = element.second.getSymbol();
                    *addr_found = element.second.getAddr();
                    distance = ndistance;
                }

            }
            else {
                matched = true;
                distance = (element.second.getAddr() <= addr) ? addr - element.second.getAddr() : element.second.getAddr() - addr;
                ret = element.second.getSymbol();
                *addr_found = element.second.getAddr();
            }
        }
    }
    return matched;
}

bool symbol_table::find_by_name(const char * name, uint32_t &ret) {
    auto i = map.find(name);
    if (i != map.end())
    {
        ret = i->second.getAddr();
        return true;
    }
    return false;

}

void symbol_table::symbol_list(cpu_debug_t *cpu, debug_outf_t debug_outf) {
    if (length() == 0)
        debug_outf("No symbols loaded");
    for (auto &element : map) {
        char addrstr[17];
        cpu->print_addr(cpu, element.second.getAddr(), addrstr, 16, false);
        debug_outf("%s=%s\n", element.second.getSymbol(), addrstr);
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
    return symtab && symtab->find_by_addr_near(addr, min, max, addr_found, *ret);
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

