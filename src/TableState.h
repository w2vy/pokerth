#ifndef TABLESTATE_H
#define TABLESTATE_H

#include <string>
#include <unordered_map>

enum class TableState {
    Idle = 0,
    Connecting = 1,
    Registration = 2,
    Playing = 3,
    Finished = 4,
    Closed = 5
};

const std::unordered_map<TableState, std::string> stateNameMap = {
    {TableState::Idle, "Idle"},
    {TableState::Connecting, "Connecting"},
    {TableState::Registration, "Registration"},
    {TableState::Playing, "Playing"},
    {TableState::Finished, "Finished"},
    {TableState::Closed, "Closed"}
};

inline const std::string stateName(TableState state) {
    auto it = stateNameMap.find(state);
    return it!= stateNameMap.end()? it->second : "Unknown";
}

#endif  // TABLESTATE_H
