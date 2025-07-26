#include "TableManager.h"

Table& TableManager::addTable(TournamentDirector* mytd, const std::string& name, const std::string& watcher) {
    size_t id = tables.size();
    Table table;
    uint32_t suffix_id = getNewSuffix();
    //table.mytd = mytd;
    table.info.name = name + " "  + std::to_string(suffix_id);
    table.info.watcher = watcher + " "  + std::to_string(suffix_id);
    table.invite = std::vector<Player>();
    tables[id] = table;
    return tables[id];
}

uint32_t TableManager::getNewSuffix() {
    static uint32_t suffix_id = 1;
    while (tables.find(suffix_id)!= tables.end()) {
        suffix_id++;
    }
    return suffix_id++;
}

std::optional<Table&> TableManager::getTable(size_t id) {
    auto it = tables.find(id);
    if (it != tables.end()) {
        return it->second;
    }
    return std::nullopt;
}

void TableManager::removeTable(size_t id) {
    tables.erase(id);
}

void TableManager::removeTable(const Table& t) {
    auto it = tables.find(t.info.game_id);
    if (it!= tables.end()) {
        tables.erase(it);
    }
}

std::vector<Table> TableManager::getTables() { // Read Only
    std::vector<Table> result;
    result.reserve(tables.size());
    for (const auto& [id, table] : tables) {
        result.push_back(table);
    }
    return result;
}

std::optional<size_t> TableManager::findTableByType(const TableType type) {
    for (const auto& [id, table] : tables) {
        if (table.info.type == type) {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<size_t> TableManager::findTableByName(const std::string& name) {
    for (const auto& [id, table] : tables) {
        if (table.info.name == name) {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<size_t> TableManager::findTableByGameId(uint32_t gameId) {
    for (const auto& [id, table] : tables) {
        if (table.info.game_id == gameId) {
            return id;
        }
    }
    return std::nullopt;
}

void TableManager::updateState(Table table, TableState newState) {
    if (table.state != newState) {
       table.state = newState;
        if (stateChangeCallback) {
            stateChangeCallback(table, newState);
        }
    }
}

void TableManager::setStateChangeCallback(std::function<void(Table, const TableState&)> callback) {
    stateChangeCallback = std::move(callback);
}
