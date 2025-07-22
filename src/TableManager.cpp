#include "TableManager.h"

void TableManager::addTable(Table table) {
    tables[table.info.game_id] = table;
}

Table TableManager::getTable(uint32_t id) {
    return tables[id];
}

void TableManager::removeTable(uint32_t id) {
    tables.erase(id);
}

std::vector<Table> TableManager::getTables() {
    std::vector<Table> tablesCopy = tables;
    return tablesCopy;
}

std::<optional_t> findTableByName(const std::string& name) {
    for (const auto& table : tables) {
        if (table.info.name == name) {
            return table.info.game_id;
        }
    }
    return std::nullopt;
}

std::<optional_t> findTableByGameId(uint32_t gameId) {
    for (const auto& table : tables) {
        if (table.info.game_id == gameId) {
            return table.info.game_id;
        }
    }
    return std::nullopt;
}

void TableManager::updateState(uint32_t id, TableState newState) {
    if (tables.find(id)!= tables.end()) {
        if (tables[id].state!= newState) {
            tables[id].state = newState;
            if (stateChangeCallbacks.find(id)!= stateChangeCallbacks.end()) {
                stateChangeCallbacks[id](newState);
            }
        }
    }
}

void TableManager::setStateChangeCallback(uint32_t id, std::function(TableState)> callback) {
    stateChangeCallbacks[id] = callback;
}
