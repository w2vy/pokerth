#pragma once
#include <optional>
#include <functional>

#include "Table.h"

class TableManager {
public:
    static constexpr size_t MAX_TABLES = 10;
    using StateChangeCallback = std::function<void(Table *table, TableState oldState, TableState newState)>;

    TableManager() = default;

    void setStateChangeCallback(StateChangeCallback cb) {
        callback_ = std::move(cb);
    }

    std::optional<size_t> allocateTable(TournamentDirector* td, const std::string& name) {
        for (size_t i = 0; i < tables_.size(); ++i) {
            if (tables_[i].state == Idle) {
                tables_[i].mytd = td;
                tables_[i].state = Registration;
                tables_[i].game_id = 0;
                tables_[i].name = name;
                tables_[i].watcher = "WatchBot";
                tables_[i].type = GameTypes::Qualifier;
                tables_[i].Invite.clear();
                return i;
            }
        }
        return std::nullopt;
    }

    void freeTable(Table *table) {
        bool is_valid = table >= tables_.data() && table < tables_.data() + tables_.size();
        if (is_valid) {
            TableState oldState = table->state;
            *table = Table();  // Reset values
            triggerCallback(table, oldState, Idle);
        }
    }

    std::vector<Table*> activeTables(void) {
        std::vector<Table*> active;
        active.reserve(tables_.size());   // avoid reallocations

        for (size_t i = 0; i < tables_.size(); ++i) {
            Table *table = &tables_[i];
            if (table->state != Idle) {
                active.push_back(table);
            }
        }
        return active;
    }

    bool updateState(Table *table, TableState newState) {
        if (table) {
            TableState oldState = table->state;
            if (oldState != newState) {
                table->state = newState;
                triggerCallback(table, oldState, newState);
            }
            return true;
        }
        return false;
    }

    Table* getTable(std::optional<size_t> index) {
        if (index.has_value() && index < tables_.size()) {
            size_t ndx = *index;
            return &tables_[ndx];
        }
        return nullptr;
    }

    std::optional<size_t> findTableByName(std::string name) const {
        for (size_t i = 0; i < tables_.size(); ++i) {
            if (tables_[i].state != Idle && tables_[i].name == name) {
                return i;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> findTableByGameId(uint32_t game_id) const {
        for (size_t i = 0; i < tables_.size(); ++i) {
            if (tables_[i].state != Idle && tables_[i].game_id == game_id) {
                return i;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> findTableByType(GameTypes type) const {
        for (size_t i = 0; i < tables_.size(); ++i) {
            if (tables_[i].state != Idle && tables_[i].type == type) {
                return i;
            }
        }
        return std::nullopt;
    }

private:
    void triggerCallback(Table *table, TableState oldState, TableState newState) {
        if (callback_) {
            callback_(table, oldState, newState);
        }
    }

    std::array<Table, MAX_TABLES> tables_;
    StateChangeCallback callback_;
};

