#ifndef TABLEMANAGER_H
#define TABLEMANAGER_H

#include "Table.h"

class TableManager {
public:
    void addTable(Table table);
    Table getTable(uint32_t id);
    void removeTable(uint32_t id);
    std::vector<Table> getTables();
    void updateState(uint32_t id, TableState newState);
    void setStateChangeCallback(uint32_t id, std::function<void(TableState)> callback);

private:
    std::map<uint32_t, Table> tables;
    std::map<uint32_t, std::function <void(TableState)>> stateChangeCallbacks;
};

#endif  // TABLEMANAGER_H