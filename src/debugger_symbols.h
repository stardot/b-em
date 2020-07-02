// debugger symbol table

#ifndef __DEBUGGER_SYMBOLS_H__
#define __DEBUGGER_SYMBOLS_H__

#include <stdint.h>

#define SYM_MAX 32
#define STRINGY(x) STRINGY2(x)
#define STRINGY2(x) #x


#ifdef __cplusplus
extern "C" {
#endif

typedef void(*debug_outf_t)(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

typedef struct cpu_debug_t cpu_debug_t;

// a bit of doding about to allow C to access CPP
#ifdef __cplusplus

#include <map>
#include <string>

    class symbol_table {
    private:
        std::map<std::string, uint32_t> map;
    public:
        void add(std::string name, uint32_t addr);
        bool find_by_addr(uint32_t addr, std::string &ret);
        bool find_by_name(std::string name, uint32_t &ret);
        bool find_by_addr_near(uint32_t addr, uint32_t min, uint32_t max, uint32_t *addr_found, std::string &ret);
        int length() { return map.size(); }

        void symbol_list(cpu_debug_t *cpu, debug_outf_t debug_outf);
    };
#else
    typedef struct symbol_table symbol_table;
#endif

#ifdef __cplusplus
    extern "C" {
#else
#include <stdbool.h>
#endif


        symbol_table* symbol_new(void);
        void symbol_free(symbol_table *symtab);
        void symbol_add(symbol_table *symtab, const char *name, uint32_t addr);
        bool symbol_find_by_addr(symbol_table *symtab, uint32_t addr, char **ret);
        bool symbol_find_by_addr_near(symbol_table *symtab, uint32_t addr, uint32_t min, uint32_t max, uint32_t *addr_found, char **ret);
        bool symbol_find_by_name(symbol_table *symtab, const char *name, uint32_t *addr, const char **endret);
        void symbol_list(symbol_table *symtab, struct cpu_debug_t *cpu, debug_outf_t debug_outf);

#ifdef __cplusplus
    }
#endif

#endif
