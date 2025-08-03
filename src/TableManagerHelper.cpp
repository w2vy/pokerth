// File: TableManagerHelper.cpp
#include "TableManager.h"
#include "TournamentDirector.h"

void TableManager::updateState(TournamentDirector* mytd, Table& table, TableState newState) {
    if (table.state!= newState) {
       table.state = newState;
        if (stateChangeCallback) {
            stateChangeCallback(mytd, table, newState);
        }
    }
}

void TableManager::setStateChangeCallback(std::function<void(TournamentDirector* mytd, Table&, const TableState&)> callback) {
    stateChangeCallback = std::move(callback);
}
