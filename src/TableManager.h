#ifndef TABLEMANAGER_H
#define TABLEMANAGER_H

#include <map>
#include <optional>
#include <functional>
#include "Table.h"

class TableManager {
public:
    Table& addTable(const std::string& name, const std::string& watcher);
    uint32_t getNewSuffix();
    std::optional<Table> getTable(size_t id);
    void removeTable(size_t id);
    void removeTable(const Table& t);
    std::vector<Table> getTables();
    std::optional<size_t> findTableByType(const TableType type);
    std::optional<size_t> findTableByName(const std::string& name);
    std::optional<size_t> findTableByGameId(uint32_t gameId);
    void updateState(Table table, TableState newState);
    void setStateChangeCallback(std::function<void(Table, const TableState&)> callback);
private:
    std::map<uint32_t, Table> tables;
    std::function<void(Table, const TableState&)> stateChangeCallback;
};

#endif  // TABLEMANAGER_H
