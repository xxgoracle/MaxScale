#ifndef LABELS_TABLE_H
#define LABELS_TABLE_H

#include <string>

struct labels_table_t
{
    const char* test_label;
    const char* mdbci_label;

};

const labels_table_t labels_table [] __attribute__((unused)) = {
        {"REPL_BACKEND", "REPL_BACKEND"},
        {"GALERA_BACKEND", "GALERA_BACKEND"},
        {"TWO_MAXSCALES", "SECOND_MAXSCALE"},
        {"COLUMNSTORE_BACKEND", "COLUMNSTORE_BACKEND"},
        };

std::string get_mdbci_lables(char * labels_string);

#endif // LABELS_TABLE_H
