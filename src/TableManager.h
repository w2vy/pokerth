#ifndef TABLEMANAGER_H
#define TABLEMANAGER_H

#include <map>
#include <vector>
#include <functional>
#include <string>
#include "Table.h"
#include <third_party/protobuf/pokerth.pb.h>

class TournamentDirector;

class TableManager {
public:
    Table& addTable(const std::string& name, const std::string& watcher);
    uint32_t getNewSuffix();
    Table& getTable(size_t id);
    void removeTable(size_t id);
    void removeTable(const Table& t);
    std::vector<Table> getTables();
    Table* pFindTableByType(const TableType type);
    Table& findTableByType(const TableType type);
    Table& findTableByName(const std::string& name);
    Table& findTableByGameId(uint32_t gameId);
    void updateState(TournamentDirector* mytd, Table& table, TableState newState);
    void setStateChangeCallback(std::function<void(TournamentDirector* mytd, Table&, const TableState&)> callback);
    std::function<PokerTHMessage(std::string name, std::string password, NetGameInfo_NetGameType gameType, int nPlayers)> createGame;
private:
    std::map<uint32_t, Table> tables;
    std::function<void(TournamentDirector* mytd, Table&, const TableState&)> stateChangeCallback;
};

#endif  // TABLEMANAGER_H
