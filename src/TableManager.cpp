#include "TableManagerHelper.cpp"

#include <stdexcept>

Table& TableManager::addTable(const std::string& name, const std::string& watcher) {
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

Table& TableManager::getTable(size_t id) {
    auto it = tables.find(id);
    if (it == tables.end()) {
        throw std::out_of_range("Table not found");
    }
    return it->second;
}

Table& TableManager::findTableByType(const TableType type) {
    for (auto& [id, table] : tables) {
        if (table.info.type == type) {
            return table;
        }
    }
    throw std::out_of_range("Table not found");
}

Table* TableManager::pFindTableByType(const TableType type) {
    for (auto& [id, table] : tables) {
        if (table.info.type == type) {
            return &table;
        }
    }
    return nullptr;
}

Table& TableManager::findTableByName(const std::string& name) {
    for (auto& [id, table] : tables) {
        if (table.info.name == name) {
            return table;
        }
    }
    throw std::out_of_range("Table not found");
}

Table& TableManager::findTableByGameId(uint32_t gameId) {
    for (auto& [id, table] : tables) {
        if (table.info.game_id == gameId) {
            return table;
        }
    }
    throw std::out_of_range("Table not found");
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
