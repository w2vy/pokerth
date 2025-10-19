#ifndef TABLESTATE_H
#define TABLESTATE_H

#include <string>
#include <unordered_map>

enum class TableState {
    Idle,
    Connecting,
    CreateGame,
    Inviting,
    Playing,
    Finished,
    Closed
};

const std::unordered_map<TableState, std::string> stateNameMap = {
    {TableState::Idle, "Idle"},
    {TableState::Connecting, "Connecting"},
    {TableState::CreateGame, "CreateGame"},
    {TableState::Inviting, "Inviting"},
    {TableState::Playing, "Playing"},
    {TableState::Finished, "Finished"},
    {TableState::Closed, "Closed"}
};

inline const std::string stateName(TableState state) {
    auto it = stateNameMap.find(state);
    return it!= stateNameMap.end()? it->second : "Unknown";
}

#endif  // TABLESTATE_H
