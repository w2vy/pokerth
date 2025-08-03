#ifndef TABLEMANAGER_H
#define TABLEMANAGER_H

#include <map>
#include <vector>
#include <functional>
#include "Table.h"

class TournamentDirector;

class TableManager {
public:
    Table& addTable(const std::string& name, const std::string& watcher);
    uint32_t getNewSuffix();
    Table& getTable(size_t id);
    void removeTable(size_t id);
    void removeTable(const Table& t);
    std::vector<Table> getTables();
    Table& findTableByType(const TableType type);
    Table& findTableByName(const std::string& name);
    Table& findTableByGameId(uint32_t gameId);
    void updateState(TournamentDirector* mytd, Table& table, TableState newState);
    void setStateChangeCallback(std::function<void(TournamentDirector* mytd, Table&, const TableState&)> callback);
private:
    std::map<uint32_t, Table> tables;
    std::function<void(TournamentDirector* mytd, Table&, const TableState&)> stateChangeCallback;
};

#endif  // TABLEMANAGER_H
