--[[
  engine.lua -- Lua execution engines for the Lua console plugin.
]]

local lgi = require('luaconsole.compat')
local GLib = lgi.GLib

local EMBEDDED_INTERPRETER_ID = 'embedded'

local EXTERNAL_LUA_WRAPPER = [[
local source_path = %s
local chunkname = %s
local f, err = io.open(source_path, 'rb')
if not f then
    io.stderr:write(tostring(err) .. '\n')
    os.exit(1)
end

local source = f:read('*a')
f:close()

local load_chunk = loadstring or load
local chunk, compile_err = load_chunk('return ' .. source, chunkname)
if not chunk then
    chunk, compile_err = load_chunk(source, chunkname)
end

if not chunk then
    io.stderr:write(tostring(compile_err) .. '\n')
    os.exit(1)
end

local results = { pcall(chunk) }
local ok = table.remove(results, 1)

if not ok then
    io.stderr:write(tostring(results[1]) .. '\n')
    os.exit(1)
end

if #results > 0 then
    local parts = {}
    for i, value in ipairs(results) do
        parts[i] = tostring(value)
    end
    io.write(table.concat(parts, '\t') .. '\n')
end
]]

local function shell_quote(value)
    return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

local function command_output(command)
    local pipe = io.popen(command .. ' 2>&1')
    if pipe == nil then
        return nil, false
    end

    local output = pipe:read('*a') or ''
    local ok = pipe:close()

    return output, ok == true
end

local function is_incomplete(err)
    return type(err) == 'string' and err:sub(-6) == "<eof>'"
end

local function compile_chunk(source, namespace, chunkname)
    local chunk, err = loadstring('return ' .. source, chunkname)
    if not chunk then
        chunk, err = loadstring(source, chunkname)
    end
    if chunk then
        setfenv(chunk, namespace)
    end
    return chunk, err
end

local function embedded_version()
    if _VERSION == 'Lua 5.1' then
        return 'Lua 5.1  Copyright (C) 1994-2012 Lua.org, PUC-Rio'
    end

    return _VERSION or 'Lua'
end

local function default_lua()
    return GLib.find_program_in_path('lua') or 'lua'
end

local function write_file(path, text)
    local f, err = io.open(path, 'wb')

    if not f then
        return nil, err
    end

    f:write(text)
    f:close()

    return true
end

local EmbeddedEngine = {}
EmbeddedEngine.__index = EmbeddedEngine

function EmbeddedEngine.new(namespace)
    return setmetatable({ namespace = namespace }, EmbeddedEngine)
end

function EmbeddedEngine:get_version()
    return embedded_version()
end

function EmbeddedEngine:reset()
end

function EmbeddedEngine:execute(command, silent_on_incomplete, chunkname)
    local chunk, err = compile_chunk(command, self.namespace, chunkname or '=console')
    if not chunk then
        if silent_on_incomplete and is_incomplete(err) then
            return 'incomplete'
        end
        return 'done', tostring(err) .. '\n', true
    end

    local results = { pcall(chunk) }
    local ok = table.remove(results, 1)
    if not ok then
        return 'done', tostring(results[1]) .. '\n', true
    elseif #results > 0 then
        local parts = {}
        for _, value in ipairs(results) do
            parts[#parts + 1] = tostring(value)
        end
        return 'done', table.concat(parts, '\t') .. '\n', false
    end

    return 'done'
end

local ExternalEngine = {}
ExternalEngine.__index = ExternalEngine

function ExternalEngine.new(interpreter)
    if interpreter == nil or interpreter == '' then
        interpreter = default_lua()
    end

    return setmetatable({ interpreter = interpreter }, ExternalEngine)
end

function ExternalEngine:get_version()
    local output = command_output(shell_quote(self.interpreter) .. ' -v')
    output = (output or ''):gsub('[\r\n]+$', '')

    if output == '' then
        return self.interpreter
    end

    return output
end

function ExternalEngine:reset()
end

function ExternalEngine:execute(command, silent_on_incomplete, chunkname)
    local source_path = os.tmpname()
    local wrapper_path = os.tmpname()
    local ok, err

    ok, err = write_file(source_path, command)
    if not ok then
        return 'done', tostring(err) .. '\n', true
    end

    ok, err = write_file(wrapper_path,
                         EXTERNAL_LUA_WRAPPER:format(string.format('%q', source_path),
                                                     string.format('%q', chunkname or '=console')))
    if not ok then
        os.remove(source_path)
        return 'done', tostring(err) .. '\n', true
    end

    local output, success = command_output(shell_quote(self.interpreter) .. ' ' .. shell_quote(wrapper_path))

    os.remove(source_path)
    os.remove(wrapper_path)

    if not success then
        if silent_on_incomplete and output and output:find('<eof>', 1, true) then
            return 'incomplete'
        end

        return 'done', output or '', true
    end

    return 'done', output or '', false
end

local Engine = {}

function Engine.new(interpreter, namespace)
    if interpreter == EMBEDDED_INTERPRETER_ID then
        return EmbeddedEngine.new(namespace)
    end

    return ExternalEngine.new(interpreter)
end

return Engine
