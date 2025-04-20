-- PokerTH Wireshark Lua dissector (extended for Announce and Init messages)
local p_pokerth = Proto("pokerth", "PokerTH Protocol")

-- Port to dissect
local tcp_port = 7234

-- Message type map
local message_type_names = {
    [1] = "AnnounceMessage",
    [2] = "InitMessage",
    [3] = "AuthServerChallengeMessage",
    [4] = "AuthClientResponseMessage",
    [5] = "AuthServerVerificationMessage",
    [6] = "InitAckMessage",
    [7] = "AvatarRequestMessage",
    [8] = "AvatarHeaderMessage",
    [9] = "AvatarDataMessage",
    [10] = "AvatarEndMessage",
    [11] = "UnknownAvatarMessage",
    [12] = "PlayerListMessage",
    [13] = "GameListNewMessage",
    [14] = "GameListUpdateMessage",
    [15] = "GameListPlayerJoinedMessage",
    [16] = "GameListPlayerLeftMessage",
    [17] = "GameListAdminChangedMessage",
    [18] = "PlayerInfoRequestMessage",
    [19] = "PlayerInfoReplyMessage",
    [20] = "SubscriptionRequestMessage",
    [21] = "JoinExistingGameMessage",
    [22] = "JoinNewGameMessage",
    [23] = "RejoinExistingGameMessage",
    [24] = "JoinGameAckMessage",
    [25] = "JoinGameFailedMessage",
    [26] = "GamePlayerJoinedMessage",
    [27] = "GamePlayerLeftMessage",
    [28] = "GameAdminChangedMessage",
    [29] = "RemovedFromGameMessage",
    [30] = "KickPlayerRequestMessage",
    [31] = "LeaveGameRequestMessage",
    [32] = "InvitePlayerToGameMessage",
    [33] = "InviteNotifyMessage",
    [34] = "RejectGameInvitationMessage",
    [35] = "RejectInvNotifyMessage",
    [36] = "StartEventMessage",
    [37] = "StartEventAckMessage",
    [38] = "GameStartInitialMessage",
    [39] = "GameStartRejoinMessage",
    [40] = "HandStartMessage",
    [41] = "PlayersTurnMessage",
    [42] = "MyActionRequestMessage",
    [43] = "YourActionRejectedMessage",
    [44] = "PlayersActionDoneMessage",
    [45] = "DealFlopCardsMessage",
    [46] = "DealTurnCardMessage",
    [47] = "DealRiverCardMessage",
    [48] = "AllInShowCardsMessage",
    [49] = "EndOfHandShowCardsMessage",
    [50] = "EndOfHandHideCardsMessage",
    [51] = "ShowMyCardsRequestMessage",
    [52] = "AfterHandShowCardsMessage",
    [53] = "EndOfGameMessage",
    [54] = "PlayerIdChangedMessage",
    [55] = "AskKickPlayerMessage",
    [56] = "AskKickDeniedMessage",
    [57] = "StartKickPetitionMessage",
    [58] = "VoteKickRequestMessage",
    [59] = "VoteKickReplyMessage",
    [60] = "KickPetitionUpdateMessage",
    [61] = "EndKickPetitionMessage",
    [62] = "StatisticsMessage",
    [63] = "ChatRequestMessage",
    [64] = "ChatMessage",
    [65] = "ChatRejectMessage",
    [66] = "DialogMessage",
    [67] = "TimeoutWarningMessage",
    [68] = "ResetTimeoutMessage",
    [69] = "ReportAvatarMessage",
    [70] = "ReportAvatarAckMessage",
    [71] = "ReportGameMessage",
    [72] = "ReportGameAckMessage",
    [73] = "ErrorMessage",
    [74] = "AdminRemoveGameMessage",
    [75] = "AdminRemoveGameAckMessage",
    [76] = "AdminBanPlayerMessage",
    [77] = "AdminBanPlayerAckMessage",
    [78] = "GameListSpectatorJoinedMessage",
    [79] = "GameListSpectatorLeftMessage",
    [80] = "GameSpectatorJoinedMessage",
    [81] = "GameSpectatorLeftMessage"
}

-- Enum mappings
local server_types = {
    [0] = "LAN",
    [1] = "InternetNoAuth",
    [2] = "InternetAuth"
}

local login_types = {
    [0] = "Guest",
    [1] = "Authenticated",
    [2] = "Unauthenticated"
}

-- Fields
local f_length = ProtoField.uint32("pokerth.length", "Packet Length", base.DEC)
local f_type = ProtoField.uint8("pokerth.type", "Message Type", base.DEC, message_type_names)

-- Version fields (used inside AnnounceMessage)
f_version_major = ProtoField.uint32("pokerth.version.major", "Major Version", base.DEC)
f_version_minor = ProtoField.uint32("pokerth.version.minor", "Minor Version", base.DEC)

-- AnnounceMessage fields
f_announce_beta_rev = ProtoField.uint32("pokerth.announce.beta_revision", "Latest Beta Revision", base.DEC)
f_announce_server_type = ProtoField.string("pokerth.announce.server_type", "Server Type")
f_announce_num_players = ProtoField.uint32("pokerth.announce.num_players", "Players On Server", base.DEC)

server_type_enum = {
    [0] = "LAN",
    [1] = "Internet (No Auth)",
    [2] = "Internet (Auth)"
}

local f_build_id = ProtoField.uint32("pokerth.build_id", "Build ID", base.DEC)
local f_login_type = ProtoField.uint32("pokerth.login", "Login Type", base.DEC, login_types)

-- Define InitMessage subtree fields
local f_init_requested_version = ProtoField.string("pokerth.init.requested_version", "Requested Version")
local f_init_build_id = ProtoField.uint32("pokerth.init.build_id", "Build ID", base.DEC)
local f_init_last_session = ProtoField.bytes("pokerth.init.last_session", "Last Session ID")
local f_init_password = ProtoField.string("pokerth.init.password", "Auth Server Password")
local f_init_login = ProtoField.uint8("pokerth.init.login", "Login Type", base.DEC, {
    [0] = "Guest Login",
    [1] = "Authenticated Login",
    [2] = "Unauthenticated Login"
})
local f_init_nickname = ProtoField.string("pokerth.init.nickname", "Nickname")
local f_init_userdata = ProtoField.bytes("pokerth.init.client_userdata", "Client User Data")
local f_init_avatarhash = ProtoField.bytes("pokerth.init.avatarhash", "Avatar Hash")

f_initack_yourSessionId = ProtoField.bytes("pokerth.initack_yourSessionId", "Your Session ID")
f_initack_yourPlayerId = ProtoField.uint32("pokerth.initack_yourPlayerId", "Your Player ID", base.DEC)
f_initack_yourAvatarHash = ProtoField.bytes("pokerth.initack_yourAvatarHash", "Your Avatar Hash")
f_initack_rejoinGameId = ProtoField.uint32("pokerth.initack_rejoinGameId", "Rejoin Game ID", base.DEC)

f_playerlist_id = ProtoField.uint32("pokerth.playerlist.id", "Player ID", base.DEC)
f_playerlist_notify = ProtoField.uint8("pokerth.playerlist.notification", "Player List Notification", base.DEC, {
    [0] = "playerListNew",
    [1] = "playerListLeft"
})

f_gamelist_game_id     = ProtoField.uint32("pokerth.gamelist.game_id", "Game ID", base.DEC)
f_gamelist_game_mode   = ProtoField.uint8("pokerth.gamelist.game_mode", "Game Mode", base.DEC)
f_gamelist_private     = ProtoField.bool("pokerth.gamelist.private", "Is Private")
f_gamelist_players     = ProtoField.bytes("pokerth.gamelist.players", "Player IDs")
f_gamelist_players_id  = ProtoField.uint32("pokerth.gamelist.player_id", "Player ID", base.DEC)
f_gamelist_admin_id    = ProtoField.uint32("pokerth.gamelist.admin_id", "Admin Player ID", base.DEC)
f_gamelist_game_info   = ProtoField.bytes("pokerth.gamelist.game_info", "NetGameInfo")
f_gamelist_spectators  = ProtoField.bytes("pokerth.gamelist.spectators", "Spectator IDs")
f_gamelist_spectator_id = ProtoField.uint32("pokerth.gamelist.spectator_id", "Spectator ID", base.DEC)

f_game_id = ProtoField.uint32("pokerth.game.id", "Game ID", base.DEC)
f_game_mode = ProtoField.uint32("pokerth.game.mode", "Game Mode", base.DEC)
f_game_is_private = ProtoField.bool("pokerth.game.private", "Is Private")
f_game_admin_id = ProtoField.uint32("pokerth.game.adminid", "Admin Player ID", base.DEC)
f_game_player_ids = ProtoField.uint32("pokerth.game.playerid", "Player ID", base.DEC)
f_game_spectator_ids = ProtoField.uint32("pokerth.game.spectatorid", "Spectator ID", base.DEC)

-- NetGameInfo fields
local f_gameinfo_name = ProtoField.string("pokerth.gameinfo.name", "Game Name")
local f_gameinfo_type = ProtoField.uint32("pokerth.gameinfo.type", "Net Game Type", base.DEC, {
    [1] = "Normal",
    [2] = "Registered Only",
    [3] = "Invite Only",
    [4] = "Ranking"
})
local f_gameinfo_max_players = ProtoField.uint32("pokerth.gameinfo.max_players", "Max Number of Players")
local f_gameinfo_raise_mode = ProtoField.uint32("pokerth.gameinfo.raise_mode", "Raise Interval Mode", base.DEC, {
    [1] = "On Hand Num",
    [2] = "On Minutes"
})
local f_gameinfo_raise_hands = ProtoField.uint32("pokerth.gameinfo.raise_hands", "Raise Every Hands")
local f_gameinfo_raise_minutes = ProtoField.uint32("pokerth.gameinfo.raise_minutes", "Raise Every Minutes")
local f_gameinfo_end_mode = ProtoField.uint32("pokerth.gameinfo.end_mode", "End Raise Mode", base.DEC, {
    [1] = "Double Blinds",
    [2] = "Raise By End Value",
    [3] = "Keep Last Blind"
})

local f_gameinfo_end_blind = ProtoField.uint32("pokerth.gameinfo.end_blind", "End Raise Small Blind Value")
local f_gameinfo_gui_speed = ProtoField.uint32("pokerth.gameinfo.gui_speed", "Proposed GUI Speed")
local f_gameinfo_delay = ProtoField.uint32("pokerth.gameinfo.delay", "Delay Between Hands (s)")
local f_gameinfo_action_timeout = ProtoField.uint32("pokerth.gameinfo.action_timeout", "Player Action Timeout (s)")
local f_gameinfo_first_blind = ProtoField.uint32("pokerth.gameinfo.first_blind", "First Small Blind")
local f_gameinfo_start_money = ProtoField.uint32("pokerth.gameinfo.start_money", "Start Money")
local f_gameinfo_manual_blinds = ProtoField.uint32("pokerth.gameinfo.manual_blinds", "Manual Blinds", base.DEC)
local f_gameinfo_allow_spectators = ProtoField.bool("pokerth.gameinfo.allow_spectators", "Allow Spectators")

f_player_info_id = ProtoField.uint32("pokerth.playerinfo.player_id", "Player ID", base.DEC)
f_playerinfo_reply_id = ProtoField.uint32("pokerth.playerinfo.id", "Player ID", base.DEC)
f_playerinfo_name = ProtoField.string("pokerth.playerinfo.name", "Player Name")
f_playerinfo_ishuman = ProtoField.bool("pokerth.playerinfo.ishuman", "Is Human")
f_playerinfo_rights = ProtoField.uint8("pokerth.playerinfo.rights", "Player Rights", base.DEC)
f_playerinfo_avatar_type = ProtoField.uint8("pokerth.playerinfo.avatar.type", "Avatar Type", base.DEC)
f_playerinfo_avatar_hash = ProtoField.bytes("pokerth.playerinfo.avatar.hash", "Avatar Hash")

f_chat_game_id = ProtoField.uint32("pokerth.chat.gameid", "Game ID", base.DEC)
f_chat_player_id = ProtoField.uint32("pokerth.chat.playerid", "Player ID", base.DEC)
f_chat_type = ProtoField.uint8("pokerth.chat.type", "Chat Type", base.DEC, {
    [0] = "Lobby",
    [1] = "Game",
    [2] = "Bot",
    [3] = "Broadcast",
    [4] = "Private"
})
f_chat_text = ProtoField.string("pokerth.chat.text", "Chat Text")

f_spectator_game_id = ProtoField.uint32("pokerth.spectator.game_id", "Spectator Game ID", base.DEC)
f_spectator_player_id = ProtoField.uint32("pokerth.spectator.player_id", "Spectator Player ID", base.DEC)

f_player_joined_gameid = ProtoField.uint32("pokerth.playerjoined.gameid", "Game ID", base.DEC)
f_player_joined_playerid = ProtoField.uint32("pokerth.playerjoined.playerid", "Player ID", base.DEC)

f_player_left_gameid = ProtoField.uint32("pokerth.playerleft.gameid", "Game ID", base.DEC)
f_player_left_playerid = ProtoField.uint32("pokerth.playerleft.playerid", "Player ID", base.DEC)

f_chatreq_target_game_id = ProtoField.uint32("pokerth.chatreq.target_game_id", "Target Game ID", base.DEC)
f_chatreq_target_player_id = ProtoField.uint32("pokerth.chatreq.target_player_id", "Target Player ID", base.DEC)
f_chatreq_text = ProtoField.string("pokerth.chatreq.text", "Chat Text")

f_error_reason = ProtoField.uint32("pokerth.error.reason", "Error Reason", base.DEC, {
    [0] = "custReserved",
    [1] = "initVersionNotSupported",
    [2] = "initServerFull",
    [3] = "initAuthFailure",
    [4] = "initPlayerNameInUse",
    [5] = "initInvalidPlayerName",
    [6] = "initServerMaintenance",
    [7] = "initBlocked",
    [8] = "avatarTooLarge",
    [9] = "invalidPacket",
    [10] = "invalidState",
    [11] = "kickedFromServer",
    [12] = "bannedFromServer",
    [13] = "blockedByServer",
    [14] = "sessionTimeout"
})

f_join_game_password = ProtoField.string("pokerth.joinnewgame.password", "Password")
f_join_game_autoleave = ProtoField.bool("pokerth.joinnewgame.autoleave", "Auto Leave")

f_join_existing_game_id = ProtoField.uint32("pokerth.joinexisting.gameid", "Game ID", base.DEC)
f_join_existing_password = ProtoField.string("pokerth.joinexisting.password", "Password")
f_join_existing_autoleave = ProtoField.bool("pokerth.joinexisting.autoleave", "Auto Leave")
f_join_existing_spectate = ProtoField.bool("pokerth.joinexisting.spectate", "Spectate Only")

f_join_failed_game_id = ProtoField.uint32("pokerth.joinfailed.gameid", "Game ID", base.DEC)
f_join_failed_reason = ProtoField.uint32("pokerth.joinfailed.reason", "Failure Reason", base.DEC, {
    [1] = "Invalid Game",
    [2] = "Game is Full",
    [3] = "Game is Running",
    [4] = "Invalid Password",
    [5] = "Not Allowed as Guest",
    [6] = "Not Invited",
    [7] = "Game Name in Use",
    [8] = "Bad Game Name",
    [9] = "Invalid Settings",
    [10] = "IP Address Blocked",
    [11] = "Rejoin Failed",
    [12] = "No Spectators Allowed"
})

f_admin_changed_game_id = ProtoField.uint32("pokerth.adminchanged.gameid", "Game ID", base.DEC)
f_admin_changed_new_admin = ProtoField.uint32("pokerth.adminchanged.newadmin", "New Admin Player ID", base.DEC)

f_game_update_game_id = ProtoField.uint32("pokerth.update.gameid", "Game ID", base.DEC)
f_game_update_game_mode = ProtoField.uint32("pokerth.update.gamemode", "Game Mode", base.DEC, {
    [1] = "Created",
    [2] = "Started",
    [3] = "Closed"
})

p_pokerth.fields = {
    -- Common header fields
    f_length, f_type,

    -- AnnounceMessage fields
    f_version_major, f_version_minor, f_announce_beta_rev, f_announce_server_type, f_announce_num_players,

    -- InitMessage fields
    f_message_type, f_init_requested_version, f_init_build_id, f_init_last_session,
    f_init_password, f_init_login, f_init_nickname, f_init_userdata, f_init_avatarhash,
    f_initack_yourSessionId, f_initack_yourPlayerId, f_initack_yourAvatarHash, f_initack_rejoinGameId,
    f_playerlist_id, f_playerlist_notify,
    f_gamelist_game_id, f_gamelist_game_mode, f_gamelist_private,
    f_gamelist_players, f_gamelist_players_id,
    f_gamelist_admin_id, f_gamelist_game_info,
    f_gamelist_spectators, f_gamelist_spectator_id,
    f_game_id, f_game_mode, f_game_is_private, f_game_admin_id, f_game_player_ids, f_game_spectator_ids,
    f_gameinfo_name, f_gameinfo_type, f_gameinfo_max_players,
    f_gameinfo_raise_mode, f_gameinfo_raise_hands, f_gameinfo_raise_minutes,
    f_gameinfo_end_mode, f_gameinfo_end_blind, f_gameinfo_gui_speed,
    f_gameinfo_delay, f_gameinfo_action_timeout, f_gameinfo_first_blind,
    f_gameinfo_start_money, f_gameinfo_manual_blinds, f_gameinfo_allow_spectators,
    f_player_info_id, f_playerinfo_reply_id, f_playerinfo_name, f_playerinfo_ishuman, f_playerinfo_rights,
    f_playerinfo_avatar_type, f_playerinfo_avatar_hash,
    f_chat_game_id, f_chat_player_id, f_chat_type, f_chat_text,
    f_spectator_game_id, f_spectator_player_id,
    f_player_joined_gameid, f_player_joined_playerid,
    f_player_left_gameid, f_player_left_playerid,
    f_chatreq_target_game_id, f_chatreq_target_player_id, f_chatreq_text,
    f_join_game_password, f_join_game_autoleave,
    f_join_existing_game_id, f_join_existing_password, f_join_existing_autoleave, f_join_existing_spectate,
    f_join_failed_game_id, f_join_failed_reason,
    f_admin_changed_game_id, f_admin_changed_new_admin,
    f_game_update_game_id, f_game_update_game_mode,
    f_error_reason
}

-- Dissector function
function read_varint(tvb, offset)
    local result = 0
    local shift = 0
    local consumed = 0
    local max_offset = tvb:len()

    while offset + consumed < max_offset and consumed < 10 do
        local b = tvb(offset + consumed, 1):uint()
        result = result + bit.lshift(bit.band(b, 0x7F), shift)
        shift = shift + 7
        consumed = consumed + 1
        if bit.band(b, 0x80) == 0 then break end
    end

    return result, consumed
end

-- Utility to decode the next protobuf field tag and length (if applicable)
function read_protobuf_field_header(tvb, offset)
    local start_offset = offset

    -- Read tag (varint encoded)
    local tag, tag_len = read_varint(tvb, offset)
    offset = offset + tag_len

    local field_number = bit.rshift(tag, 3)
    local wire_type = bit.band(tag, 0x07)

    local length = 0
    local length_len = 0

    -- Handle length-delimited (wire type 2)
    if wire_type == 2 then
        length, length_len = read_varint(tvb, offset)
        offset = offset + length_len
    end

    return {
        field_number = field_number,
        wire_type = wire_type,
        tag_len = tag_len,
        length = length,
        length_len = length_len,
        total_len = offset - start_offset,
        next_offset = offset
    }
end

function parse_net_game_info(tvb, tree)
    local offset = 0

    while offset < tvb:len() do
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        offset = offset + 1

        if field_number == 1 and wire_type == 2 then -- string gameName
            local len, len_size = read_varint(tvb, offset)
            offset = offset + len_size
            tree:add(f_gameinfo_name, tvb(offset, len))
            offset = offset + len

        elseif field_number == 2 and wire_type == 0 then -- enum netGameType
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_type, tvb(offset, size)):append_text(" (" .. tostring(value) .. ")")
            offset = offset + size

        elseif field_number == 3 and wire_type == 0 then -- uint32 maxNumPlayers
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_max_players, tvb(offset, size))
            offset = offset + size

        elseif field_number == 4 and wire_type == 0 then -- enum raiseIntervalMode
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_raise_mode, tvb(offset, size)):append_text(" (" .. tostring(value) .. ")")
            offset = offset + size

        elseif field_number == 5 and wire_type == 0 then -- optional raiseEveryHands
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_raise_hands, tvb(offset, size))
            offset = offset + size

        elseif field_number == 6 and wire_type == 0 then -- optional raiseEveryMinutes
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_raise_minutes, tvb(offset, size))
            offset = offset + size

        elseif field_number == 7 and wire_type == 0 then -- enum endRaiseMode
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_end_mode, tvb(offset, size)):append_text(" (" .. tostring(value) .. ")")
            offset = offset + size

        elseif field_number == 8 and wire_type == 0 then -- optional endRaiseSmallBlindValue
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_end_blind, tvb(offset, size))
            offset = offset + size

        elseif field_number == 9 and wire_type == 0 then -- uint32 proposedGuiSpeed
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_gui_speed, tvb(offset, size))
            offset = offset + size

        elseif field_number == 10 and wire_type == 0 then -- uint32 delayBetweenHands
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_delay, tvb(offset, size))
            offset = offset + size

        elseif field_number == 11 and wire_type == 0 then -- uint32 playerActionTimeout
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_action_timeout, tvb(offset, size))
            offset = offset + size

        elseif field_number == 12 and wire_type == 0 then -- uint32 firstSmallBlind
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_first_blind, tvb(offset, size))
            offset = offset + size

        elseif field_number == 13 and wire_type == 0 then -- uint32 startMoney
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_start_money, tvb(offset, size))
            offset = offset + size

        elseif field_number == 14 and wire_type == 2 then -- repeated uint32 manualBlinds [packed = true]
            local len, len_size = read_varint(tvb, offset)
            offset = offset + len_size
            local end_offset = offset + len
            while offset < end_offset do
                local value, size = read_varint(tvb, offset)
                tree:add(f_gameinfo_manual_blinds, tvb(offset, size)):append_text(" (" .. tostring(value) .. ")")
                offset = offset + size
            end

        elseif field_number == 15 and wire_type == 0 then -- optional bool allowSpectators
            local value, size = read_varint(tvb, offset)
            tree:add(f_gameinfo_allow_spectators, tvb(offset, size))
            offset = offset + size

        else
            -- Unknown field, skip or show expert info
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unknown NetGameInfo field %d (wire type %d)", field_number, wire_type))
            break
        end
    end
end

function server_type_to_string(val)
    if val == 0 then return "LAN"
    elseif val == 1 then return "Internet (No Auth)"
    elseif val == 2 then return "Internet (Auth)"
    else return "Unknown"
    end
end

function parse_version(tvb, tree)
    local offset = 0

    while offset < tvb:len() do
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        --tree:add(tvb(offset, 1), string.format("Version Tag: field=%d, wire=%d", field_number, wire_type))
        offset = offset + 1

        if wire_type ~= 0 then
            tree:add_expert_info(PI_MALFORMED, PI_ERROR,
                string.format("Unexpected wire type %d in version (field %d)", wire_type, field_number))
            break
        end

        local val, len = read_varint(tvb, offset)

        if field_number == 1 then
            tree:add(f_version_major, tvb(offset, len), val)
        elseif field_number == 2 then
            tree:add(f_version_minor, tvb(offset, len), val)
        else
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unknown field %d in Version message", field_number))
        end

        offset = offset + len
    end
end

-- Parse the full announce message (after message type and length have been read)
function parse_announce_message(tvb, tree)
    local offset = 0
    local announce_end = tvb:len()

    local subtree = tree:add(tvb(), "AnnounceMessage")

    while offset < announce_end do
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        offset = offset + 1

        if field_number == 1 and wire_type == 2 then  -- protocolVersion
            local len, len_len = read_varint(tvb, offset)
            offset = offset + len_len
            local version_tvb = tvb(offset, len)
            local version_tree = subtree:add(version_tvb, "Protocol Version")
            parse_version(version_tvb, version_tree)
            offset = offset + len

        elseif field_number == 2 and wire_type == 2 then  -- latestGameVersion
            local len, len_len = read_varint(tvb, offset)
            offset = offset + len_len
            local version_tvb = tvb(offset, len)
            local version_tree = subtree:add(version_tvb, "Latest Game Version")
            parse_version(version_tvb, version_tree)
            offset = offset + len

        elseif field_number == 3 and wire_type == 0 then  -- latestBetaRevision
            local val, len = read_varint(tvb, offset)
            subtree:add(f_announce_beta_rev, tvb(offset, len), val)
            offset = offset + len

        elseif field_number == 4 and wire_type == 0 then  -- serverType
            local val, len = read_varint(tvb, offset)
            subtree:add(f_announce_server_type, tvb(offset, len), server_type_enum[val] or "Unknown (" .. val .. ")")
            offset = offset + len

        elseif field_number == 5 and wire_type == 0 then  -- numPlayersOnServer
            local val, len = read_varint(tvb, offset)
            subtree:add(f_announce_num_players, tvb(offset, len), val)
            offset = offset + len

        else
            subtree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unhandled field %d with wire type %d tag %x offset %d", field_number, wire_type, tag, offset))
            break
        end
    end
end

function parse_init_message(tvb, tree)
    local offset = 0
    local subtree = tree:add(tvb(), "InitMessage")
    
    while offset < tvb:len() do
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        tree:add(tvb(offset, 1), string.format("Init Tag: field=%d, wire=%d", field_number, wire_type))
        offset = offset + 1

        if field_number == 1 and wire_type == 2 then -- requested_version
            local len, len_len = read_varint(tvb, offset)
            offset = offset + len_len
            local sub_tvb = tvb(offset, len)
            local version_tree = tree:add(f_init_requested_version, sub_tvb)
            parse_version(sub_tvb, version_tree)
            offset = offset + len

        elseif field_number == 2 and wire_type == 0 then -- build_id
            local val, len = read_varint(tvb, offset)
            tree:add(f_init_build_id, tvb(offset, len), val)
            offset = offset + len

        elseif field_number == 3 and wire_type == 2 then -- last_session_id
            local len, len_len = read_varint(tvb, offset)
            offset = offset + len_len
            tree:add(f_init_last_session, tvb(offset, len))
            offset = offset + len

        elseif field_number == 4 and wire_type == 2 then -- password (not present in your example)
            local len, len_len = read_varint(tvb, offset)
            offset = offset + len_len
            tree:add(f_init_password, tvb(offset, len))
            offset = offset + len

        elseif field_number == 5 and wire_type == 0 then -- login_type
            local val, len = read_varint(tvb, offset)
            tree:add(f_init_login, tvb(offset, len), val)
            offset = offset + len

        elseif field_number == 6 and wire_type == 2 then -- nickname
            local len, len_len = read_varint(tvb, offset)
            offset = offset + len_len
            tree:add(f_init_nickname, tvb(offset, len), tvb(offset, len):string())
            offset = offset + len

        elseif field_number == 7 and wire_type == 2 then -- userdata (not in this msg)
            local len, len_len = read_varint(tvb, offset)
            offset = offset + len_len
            tree:add(f_init_userdata, tvb(offset, len))
            offset = offset + len

        elseif field_number == 8 and wire_type == 2 then -- avatarHash (not in this msg)
            local len, len_len = read_varint(tvb, offset)
            offset = offset + len_len
            tree:add(f_init_avatarhash, tvb(offset, len))
            offset = offset + len

        else
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unknown field %d (wire type %d)", field_number, wire_type))
            break
        end
    end
end

function parse_init_ack_message(tvb, tree)
    local offset = 0
    local subtree = tree:add(p_pokerth, tvb, "InitAckMessage")

    local pos = 0
    while pos < tvb:len() do
        local tag = tvb(pos, 1):uint()
        local field = bit.rshift(tag, 3)
        local wire = bit.band(tag, 0x07)
        pos = pos + 1

        if field == 1 and wire == 2 then
            local len, len_len = read_varint(tvb, pos)
            pos = pos + len_len
            subtree:add(f_initack_yourSessionId, tvb(pos, len))
            pos = pos + len
        elseif field == 2 and wire == 0 then
            local val, vlen = read_varint(tvb, pos)
            subtree:add(f_initack_yourPlayerId, tvb(pos, vlen), val)
            pos = pos + vlen
        elseif field == 3 and wire == 2 then
            local len, len_len = read_varint(tvb, pos)
            pos = pos + len_len
            subtree:add(f_initack_yourAvatarHash, tvb(pos, len))
            pos = pos + len
        elseif field == 4 and wire == 0 then
            local val, vlen = read_varint(tvb, pos)
            subtree:add(f_initack_rejoinGameId, tvb(pos, vlen), val)
            pos = pos + vlen
        else
            subtree:add_expert_info(PI_UNDECODED, PI_NOTE, string.format("Unknown field %d (wire type %d)", field, wire))
            break
        end
    end
end

function parse_player_list_message(tvb, tree)
    local offset = 0
    while offset < tvb:len() do
        local tag_byte = tvb(offset,1):uint()
        local field_number = bit.rshift(tag_byte, 3)
        local wire_type = bit.band(tag_byte, 0x07)
        offset = offset + 1

        if field_number == 1 and wire_type == 0 then  -- playerId
            local value, size = read_varint(tvb, offset)
            tree:add(f_playerlist_id, tvb(offset, size), value)
            offset = offset + size

        elseif field == 13 and wire == 2 then -- playerId
            local len, len_len = read_varint(tvb, pos)
            pos = pos + len_len
            tree:add(f_playerlist_id, tvb(pos, len))
            pos = pos + len
        elseif field_number == 2 and wire_type == 0 then  -- playerListNotification
            local value, size = read_varint(tvb, offset)
            tree:add(f_playerlist_notify, tvb(offset, size), value)
            offset = offset + size

        else
            tree:add_expert_info(PI_MALFORMED, PI_WARN, string.format("Unknown %x field %d (wire type %d)", tag_byte, field_number, wire_type))
            break
        end
    end
end

function parse_game_list_new_message(tvb, tree)
    local offset = 0

    while offset < tvb:len() do
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        offset = offset + 1

        if field_number == 1 and wire_type == 0 then -- gameId
            local value, size = read_varint(tvb, offset)
            tree:add(f_gamelist_game_id, tvb(offset, size))
            offset = offset + size

        elseif field_number == 2 and wire_type == 0 then -- gameMode (enum)
            local value, size = read_varint(tvb, offset)
            tree:add(f_gamelist_game_mode, tvb(offset, size)):append_text(" (" .. tostring(value) .. ")")
            offset = offset + size

        elseif field_number == 3 and wire_type == 0 then -- isPrivate (bool)
            local value, size = read_varint(tvb, offset)
            tree:add(f_gamelist_private, tvb(offset, size))
            offset = offset + size

        elseif field_number == 4 and wire_type == 2 then -- repeated uint32 playerIds [packed]
            local len, len_size = read_varint(tvb, offset)
            offset = offset + len_size
            local end_offset = offset + len
            while offset < end_offset do
                local value, size = read_varint(tvb, offset)
                tree:add(f_gamelist_players_id, tvb(offset, size)):append_text(" (" .. tostring(value) .. ")")
                offset = offset + size
            end

        elseif field_number == 5 and wire_type == 0 then -- adminPlayerId
            local value, size = read_varint(tvb, offset)
            tree:add(f_gamelist_admin_id, tvb(offset, size))
            offset = offset + size

        elseif field_number == 6 and wire_type == 2 then -- NetGameInfo
            local len, len_size = read_varint(tvb, offset)
            offset = offset + len_size
            local netgameinfo_tree = tree:add(f_gamelist_game_info, tvb(offset, len), "NetGameInfo")
            parse_net_game_info(tvb(offset, len):tvb(), netgameinfo_tree)
            offset = offset + len

        elseif field_number == 7 and wire_type == 2 then -- repeated uint32 spectatorIds [packed]
            local len, len_size = read_varint(tvb, offset)
            offset = offset + len_size
            local end_offset = offset + len
            while offset < end_offset do
                local value, size = read_varint(tvb, offset)
                tree:add(f_gamelist_spectator_id, tvb(offset, size)):append_text(" (" .. tostring(value) .. ")")
                offset = offset + size
            end

        else
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unknown GameListNewMessage field %d (wire type %d)", field_number, wire_type))
            break
        end
    end
end

function parse_player_info_request_message(tvb, tree)
    local offset = 0
    local tvb_len = tvb:len()

    while offset < tvb_len do
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        offset = offset + 1

        if field_number == 1 and wire_type == 2 then -- packed repeated uint32
            local len, len_len = read_varint(tvb, offset)
            offset = offset + len_len

            local end_offset = offset + len
            --local packed_tree = tree:add(tvb(offset, len), "Player IDs")
            tree:add(tvb(offset, len), "Player IDs")

            while offset < end_offset do
                local value, size = read_varint(tvb, offset)
                --packed_tree:add(f_player_info_id, tvb(offset, size), value)
                tree:add(f_player_info_id, tvb(offset, size), value)
                offset = offset + size
            end
        else
            tree:add_expert_info(PI_MALFORMED, PI_ERROR,
                string.format("Unexpected field %d with wire type %d tag=%x offset=%d", field_number, wire_type, tag, offset))
            break
        end
    end
end

function parse_player_info_reply_message(tvb, tree)
    local offset = 0
    local msg_len = tvb:len()
    local subtree = tree:add(tvb(), "PlayerInfoReplyMessage")

    while offset < msg_len do
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        offset = offset + 1

        --subtree:add_expert_info(PI_INFO, PI_COMMENT, string.format("tag %x offset %d field %d wire %d", tag, offset, field_number, wire_type))
        if field_number == 1 and wire_type == 0 then
            local value, size = read_varint(tvb, offset)
            subtree:add(f_playerinfo_reply_id, tvb(offset, size), value)
            offset = offset + size

        elseif field_number == 2 and wire_type == 2 then
            local len, len_len = read_varint(tvb, offset)
            offset = offset + len_len
            local info_tvb = tvb(offset, len)
            local info_tree = subtree:add(info_tvb, "PlayerInfoData")

            local ioff = 0
            while ioff < info_tvb:len() do
                local tag = info_tvb(ioff, 1):uint()
                local fn = bit.rshift(tag, 3)
                local wt = bit.band(tag, 0x07)
                ioff = ioff + 1

                if fn == 1 and wt == 2 then
                    local slen, slen_len = read_varint(info_tvb, ioff)
                    ioff = ioff + slen_len
                    info_tree:add(f_playerinfo_name, info_tvb(ioff, slen))
                    ioff = ioff + slen

                elseif fn == 2 and wt == 0 then
                    local val, size = read_varint(info_tvb, ioff)
                    info_tree:add(f_playerinfo_ishuman, info_tvb(ioff, size), val)
                    ioff = ioff + size

                elseif fn == 3 and wt == 0 then
                    local val, size = read_varint(info_tvb, ioff)
                    info_tree:add(f_playerinfo_rights, info_tvb(ioff, size), val)
                    ioff = ioff + size

                elseif fn == 5 and wt == 2 then
                    local alen, alen_len = read_varint(info_tvb, ioff)
                    ioff = ioff + alen_len
                    local avatar_tvb = info_tvb(ioff, alen)
                    local avatar_tree = info_tree:add(avatar_tvb, "AvatarData")
                    local aoff = 0

                    while aoff < avatar_tvb:len() do
                        local atag = avatar_tvb(aoff, 1):uint()
                        local afn = bit.rshift(atag, 3)
                        local awt = bit.band(atag, 0x07)
                        aoff = aoff + 1

                        if afn == 1 and awt == 0 then
                            local val, size = read_varint(avatar_tvb, aoff)
                            avatar_tree:add(f_playerinfo_avatar_type, avatar_tvb(aoff, size), val)
                            aoff = aoff + size
                        elseif afn == 2 and awt == 2 then
                            local hlen, hlen_len = read_varint(avatar_tvb, aoff)
                            aoff = aoff + hlen_len
                            avatar_tree:add(f_playerinfo_avatar_hash, avatar_tvb(aoff, hlen))
                            aoff = aoff + hlen
                        else
                            break
                        end
                    end

                    ioff = ioff + alen
                else
                    break
                end
            end

            offset = offset + len
        else
            break
        end
    end
end

function parse_chat_message(tvb, tree)
    local offset = 0
    while offset < tvb:len() do
        local tag_offset = offset
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        offset = offset + 1

        --tree:add_expert_info(PI_INFO, PI_COMMENT, string.format("tag %x offset %d field %d wire %d", tag, offset, field_number, wire_type))
        if field_number == 1 and wire_type == 0 then  -- gameId
            local value, size = read_varint(tvb, offset)
            tree:add(f_chat_game_id, tvb(offset, size), value)
            offset = offset + size

        elseif field_number == 2 and wire_type == 0 then  -- playerId
            local value, size = read_varint(tvb, offset)
            tree:add(f_chat_player_id, tvb(offset, size), value)
            offset = offset + size

        elseif field_number == 3 and wire_type == 0 then  -- chatType
            local value, size = read_varint(tvb, offset)
            --offset = offset + size
            tree:add(f_chat_type, tvb(offset, size), value)
            offset = offset + size

        elseif field_number == 4 and wire_type == 2 then  -- chatText
            local len, size = read_varint(tvb, offset)
            offset = offset + size
            tree:add(f_chat_text, tvb(offset, len), "Chat Text: " .. tvb(offset, len):string())
            offset = offset + len

        else
            -- Unknown field
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unknown field: %d (wire type %d) at offset %d", field_number, wire_type, tag_offset))
            break
        end
    end
end

function parse_game_list_spectator_joined_message(tvb, tree)
    local offset = 0

    while offset < tvb:len() do
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        offset = offset + 1

        if field_number == 1 and wire_type == 0 then  -- gameId
            local value, size = read_varint(tvb, offset)
            tree:add(f_spectator_game_id, tvb(offset, size), value)
            offset = offset + size

        elseif field_number == 2 and wire_type == 0 then  -- playerId
            local value, size = read_varint(tvb, offset)
            tree:add(f_spectator_player_id, tvb(offset, size), value)
            offset = offset + size

        else
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unhandled field: %d (wire type %d)", field_number, wire_type))
            break
        end
    end
end

function parse_game_list_player_joined_message(tvb, tree)
    local offset = 0

    -- Parse gameId (field 1)
    local tag = tvb(offset, 1):uint()
    local field_number = bit.rshift(tag, 3)
    local wire_type = bit.band(tag, 0x07)
    offset = offset + 1

    if field_number == 1 and wire_type == 0 then
        local game_id, varint_len = read_varint(tvb, offset)
        tree:add(f_player_joined_gameid, tvb(offset, varint_len), game_id)
        offset = offset + varint_len
    end

    -- Parse playerId (field 2)
    tag = tvb(offset, 1):uint()
    field_number = bit.rshift(tag, 3)
    wire_type = bit.band(tag, 0x07)
    offset = offset + 1

    if field_number == 2 and wire_type == 0 then
        local player_id, varint_len = read_varint(tvb, offset)
        tree:add(f_player_joined_playerid, tvb(offset, varint_len), player_id)
        offset = offset + varint_len
    end
end

function parse_game_list_player_left_message(tvb, tree)
    local offset = 0

    -- Parse gameId (field 1)
    local tag = tvb(offset, 1):uint()
    local field_number = bit.rshift(tag, 3)
    local wire_type = bit.band(tag, 0x07)
    offset = offset + 1

    if field_number == 1 and wire_type == 0 then
        local game_id, varint_len = read_varint(tvb, offset)
        tree:add(f_player_left_gameid, tvb(offset, varint_len), game_id)
        offset = offset + varint_len
    end

    -- Parse playerId (field 2)
    tag = tvb(offset, 1):uint()
    field_number = bit.rshift(tag, 3)
    wire_type = bit.band(tag, 0x07)
    offset = offset + 1

    if field_number == 2 and wire_type == 0 then
        local player_id, varint_len = read_varint(tvb, offset)
        tree:add(f_player_left_playerid, tvb(offset, varint_len), player_id)
        offset = offset + varint_len
    end
end

function parse_chat_request_message(tvb, tree)
    local offset = 0
    local tvb_len = tvb:len()

    while offset < tvb_len do
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        offset = offset + 1

        if field_number == 1 and wire_type == 0 then
            -- targetGameId
            local value, len = read_varint(tvb, offset)
            tree:add(f_chatreq_target_game_id, tvb(offset, len), value)
            offset = offset + len

        elseif field_number == 2 and wire_type == 0 then
            -- targetPlayerId
            local value, len = read_varint(tvb, offset)
            tree:add(f_chatreq_target_player_id, tvb(offset, len), value)
            offset = offset + len

        elseif field_number == 3 and wire_type == 2 then
            -- chatText (length-delimited string)
            local strlen, len_len = read_varint(tvb, offset)
            offset = offset + len_len

            if offset + strlen > tvb_len then
                tree:add_expert_info(PI_MALFORMED, PI_ERROR, "Truncated chatText string")
                break
            end

            tree:add(f_chatreq_text, tvb(offset, strlen))
            offset = offset + strlen

        else
            tree:add_expert_info(PI_MALFORMED, PI_WARN,
                string.format("Unknown or unsupported field: %d (wire type %d)", field_number, wire_type))
            break
        end
    end
end

function parse_error_message(tvb, tree)
    local offset = 0
    local tvb_len = tvb:len()

    while offset < tvb_len do
        local tag = tvb(offset, 1):uint()
        local field_number = bit.rshift(tag, 3)
        local wire_type = bit.band(tag, 0x07)
        offset = offset + 1

        if field_number == 1 and wire_type == 0 then
            -- errorReason (enum, varint)
            local value, len = read_varint(tvb, offset)
            tree:add(f_error_reason, tvb(offset, len), value)
            offset = offset + len
        else
            tree:add_expert_info(PI_MALFORMED, PI_WARN,
                string.format("Unknown or unsupported field: %d (wire type %d)", field_number, wire_type))
            break
        end
    end
end

function parse_join_new_game_message(tvb, tree)
    local offset = 0

    while offset < tvb:len() do
        local header = read_protobuf_field_header(tvb, offset)

        if header.field_number == 1 and header.wire_type == 2 then
            local sub_tvb = tvb(header.next_offset, header.length)
            local gi_tree = tree:add("Game Info")
            parse_net_game_info(sub_tvb, gi_tree)
            offset = header.next_offset + header.length

        elseif header.field_number == 2 and header.wire_type == 2 then
            local str = tvb(header.next_offset, header.length):string()
            tree:add(f_join_game_password, tvb(header.next_offset, header.length), str)
            offset = header.next_offset + header.length

        elseif header.field_number == 3 and header.wire_type == 0 then
            local val, len = read_varint(tvb, header.next_offset)
            tree:add(f_join_game_autoleave, tvb(header.next_offset, len), val ~= 0)
            offset = header.next_offset + len

        else
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unhandled field in JoinNewGameMessage: field=%d wire=%d", header.field_number, header.wire_type))
            -- try to skip unknown field if possible
            offset = header.next_offset
        end
    end
end

function parse_join_existing_game_message(tvb, tree)
    local offset = 0

    while offset < tvb:len() do
        local header = read_protobuf_field_header(tvb, offset)

        if header.field_number == 1 and header.wire_type == 0 then
            local val, len = read_varint(tvb, header.next_offset)
            tree:add(f_join_existing_game_id, tvb(header.next_offset, len), val)
            offset = header.next_offset + len

        elseif header.field_number == 2 and header.wire_type == 2 then
            local str = tvb(header.next_offset, header.length):string()
            tree:add(f_join_existing_password, tvb(header.next_offset, header.length), str)
            offset = header.next_offset + header.length

        elseif header.field_number == 3 and header.wire_type == 0 then
            local val, len = read_varint(tvb, header.next_offset)
            tree:add(f_join_existing_autoleave, tvb(header.next_offset, len), val ~= 0)
            offset = header.next_offset + len

        elseif header.field_number == 4 and header.wire_type == 0 then
            local val, len = read_varint(tvb, header.next_offset)
            tree:add(f_join_existing_spectate, tvb(header.next_offset, len), val ~= 0)
            offset = header.next_offset + len

        else
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unhandled field in JoinExistingGameMessage: field=%d wire=%d", header.field_number, header.wire_type))
            offset = header.next_offset + (header.length or 0)
        end
    end
end

function parse_join_game_failed_message(tvb, tree)
    local offset = 0

    while offset < tvb:len() do
        local header = read_protobuf_field_header(tvb, offset)

        if header.field_number == 1 and header.wire_type == 0 then
            local val, len = read_varint(tvb, header.next_offset)
            tree:add(f_join_failed_game_id, tvb(header.next_offset, len), val)
            offset = header.next_offset + len

        elseif header.field_number == 2 and header.wire_type == 0 then
            local val, len = read_varint(tvb, header.next_offset)
            tree:add(f_join_failed_reason, tvb(header.next_offset, len), val)
            offset = header.next_offset + len

        else
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unhandled field in JoinGameFailedMessage: field=%d wire=%d", header.field_number, header.wire_type))
            offset = header.next_offset + (header.length or 0)
        end
    end
end

function parse_game_list_admin_changed_message(tvb, tree)
    local offset = 0

    while offset < tvb:len() do
        local header = read_protobuf_field_header(tvb, offset)

        if header.field_number == 1 and header.wire_type == 0 then
            local val, len = read_varint(tvb, header.next_offset)
            tree:add(f_admin_changed_game_id, tvb(header.next_offset, len), val)
            offset = header.next_offset + len

        elseif header.field_number == 2 and header.wire_type == 0 then
            local val, len = read_varint(tvb, header.next_offset)
            tree:add(f_admin_changed_new_admin, tvb(header.next_offset, len), val)
            offset = header.next_offset + len

        else
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unhandled field in GameListAdminChangedMessage: field=%d wire=%d", header.field_number, header.wire_type))
            offset = header.next_offset + (header.length or 0)
        end
    end
end

function parse_game_list_update_message(tvb, tree)
    local offset = 0

    while offset < tvb:len() do
        local header = read_protobuf_field_header(tvb, offset)

        if header.field_number == 1 and header.wire_type == 0 then
            local val, len = read_varint(tvb, header.next_offset)
            tree:add(f_game_update_game_id, tvb(header.next_offset, len), val)
            offset = header.next_offset + len

        elseif header.field_number == 2 and header.wire_type == 0 then
            local val, len = read_varint(tvb, header.next_offset)
            tree:add(f_game_update_game_mode, tvb(header.next_offset, len), val)
            offset = header.next_offset + len

        else
            tree:add_expert_info(PI_UNDECODED, PI_NOTE,
                string.format("Unhandled field in GameListUpdateMessage: field=%d wire=%d", header.field_number, header.wire_type))
            offset = header.next_offset + (header.length or 0)
        end
    end
end

-- Dispatcher function table
local MESSAGE_TYPE_ANNOUNCE = 1
local MESSAGE_TYPE_INIT = 2
local MESSAGE_TYPE_INIT_ACK = 6
local MESSAGE_TYPE_PLAYERLIST = 12
local MESSAGE_TYPE_GAME_LIST_NEW = 13
local MESSAGE_TYPE_GAME_LIST_UPDATE = 14
local MESSAGE_TYPE_PLAYER_JOINED = 15
local MESSAGE_TYPE_PLAYER_LEFT = 16
local MESSAGE_TYPE_GAME_LIST_ADMIN_CHANGE = 17
local MESSAGE_TYPE_PLAYER_INFO_REQUEST = 18
local MESSAGE_TYPE_PLAYER_INFO_REPLY = 19
local MESSAGE_TYPE_JOIN_EXISTING_GAME = 21
local MESSAGE_TYPE_JOIN_NEW_GAME = 22
local MESSAGE_TYPE_JOIN_GAME_FAILED = 25
local MESSAGE_TYPE_CHAT_REQUEST = 63
local MESSAGE_TYPE_CHAT_MESSAGE = 64
local MESSAGE_TYPE_ERROR = 73
local MESSAGE_TYPE_SPECTATOR_JOINED = 78

local message_parsers = {
    [MESSAGE_TYPE_ANNOUNCE] = parse_announce_message,
    [MESSAGE_TYPE_INIT] = parse_init_message,
    [MESSAGE_TYPE_INIT_ACK] = parse_init_ack_message,
    [MESSAGE_TYPE_PLAYERLIST] = parse_player_list_message,
    [MESSAGE_TYPE_GAME_LIST_NEW] = parse_game_list_new_message,
    [MESSAGE_TYPE_GAME_LIST_UPDATE] = parse_game_list_update_message,
    [MESSAGE_TYPE_PLAYER_JOINED] = parse_game_list_player_joined_message,
    [MESSAGE_TYPE_PLAYER_LEFT] = parse_game_list_player_left_message,
    [MESSAGE_TYPE_GAME_LIST_ADMIN_CHANGE] = parse_game_list_admin_changed_message,
    [MESSAGE_TYPE_PLAYER_INFO_REQUEST] = parse_player_info_request_message,
    [MESSAGE_TYPE_PLAYER_INFO_REPLY] = parse_player_info_reply_message,
    [MESSAGE_TYPE_JOIN_EXISTING_GAME] = parse_join_existing_game_message,
    [MESSAGE_TYPE_JOIN_NEW_GAME] = parse_join_new_game_message,
    [MESSAGE_TYPE_JOIN_GAME_FAILED] = parse_join_game_failed_message,
    [MESSAGE_TYPE_CHAT_REQUEST] = parse_chat_request_message,
    [MESSAGE_TYPE_CHAT_MESSAGE] = parse_chat_message,
    [MESSAGE_TYPE_SPECTATOR_JOINED] = parse_game_list_spectator_joined_message,
    [MESSAGE_TYPE_ERROR] = parse_error_message
}

function p_pokerth.dissector(tvb, pinfo, tree)
    pinfo.cols.protocol = "PokerTH"

    local subtree = tree:add(p_pokerth, tvb(), "PokerTH Protocol")
    local offset = 0
    local tvb_len = tvb:len()

    -- Loop through potential multiple messages in one TCP segment
    while offset + 4 <= tvb_len do
        local msg_len = tvb(offset, 4):uint()
        local total_len = 4 + msg_len

        -- Check if we have a full message, otherwise ask for more data
        if offset + total_len > tvb_len then
            -- Not enough data yet: request more from TCP reassembly
            pinfo.desegment_len = (offset + total_len) - tvb_len
            pinfo.desegment_offset = offset
            return
        end

        local msg_tvb = tvb(offset + 4, msg_len)
        local msg_tree = subtree:add(p_pokerth, msg_tvb, "PokerTH Protobuf Message")
        parse_pokerth_message(msg_tvb, msg_tree)

        offset = offset + total_len
    end
end

function parse_pokerth_message(tvb, tree)
    if tvb:len() == 0 then
        tree:add_expert_info(PI_MALFORMED, PI_ERROR, "Empty protobuf message")
        return
    end

    local offset = 0
    local start_offset = offset  -- Save this to highlight the full message

    -- Step 1: Read message type (field 1, varint)
    local tag = tvb(offset, 1):uint()
    local field_number = bit.rshift(tag, 3)
    local wire_type = bit.band(tag, 0x07)
    offset = offset + 1

    if field_number ~= 1 or wire_type ~= 0 then
        tree:add_expert_info(PI_MALFORMED, PI_ERROR,
            string.format("Expected message type at field 1 (varint), got field=%d wire=%d", field_number, wire_type))
        return
    end

    -- Step 2: Read the message type
    local msg_type, varint_len = read_varint(tvb, offset)
    offset = offset + varint_len

    local header = read_protobuf_field_header(tvb, offset)
    
    local msg_tree = tree:add(p_pokerth, tvb(start_offset), string.format("PokerTH MessageType %d field=%d wire=%d len=%d tag=%x len=%d", msg_type, header.field_number, header.wire_type, header.total_len, tag, header.length))

    local parser = message_parsers[msg_type]
    if parser then
        if offset + header.length > tvb:len() then
            tree:add_expert_info(PI_MALFORMED, PI_ERROR, "Truncated inner message body")
            return
        end
        local tvb_msg = tvb(header.next_offset, header.length)
        parser(tvb_msg, msg_tree)
    else
        msg_tree:add_expert_info(PI_UNDECODED, PI_NOTE,
            string.format("Unknown or unhandled message type %d", msg_type))
    end
end

-- Register the dissector
local tcp_table = DissectorTable.get("tcp.port")
tcp_table:add(tcp_port, p_pokerth)

