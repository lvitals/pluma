-- Restricted snippets transformation runtime.
-- Only the named operations below are reachable; snippet text is never loaded
-- or evaluated as Lua source.
local operation, value = arg[1], arg[2] or ""

local words = {}
for word in value:gmatch("[%w]+") do words[#words + 1] = word end

local operations
operations = {
  upper = function() return value:upper() end,
  lower = function() return value:lower() end,
  snake = function() return value:gsub("(%l)(%u)", "%1_%2"):gsub("%-", "_"):lower() end,
  type = function()
    local snake = value:gsub("(%l)(%u)", "%1_%2"):gsub("%-", "_"):upper()
    local first, rest = snake:match("^([^_]+)_(.+)$")
    return first and (first .. "_TYPE_" .. rest) or (snake .. "_TYPE")
  end,
  is = function()
    local snake = value:gsub("(%l)(%u)", "%1_%2"):gsub("%-", "_"):upper()
    local first, rest = snake:match("^([^_]+)_(.+)$")
    return first and (first .. "_IS_" .. rest) or (snake .. "_IS")
  end,
  camel = function()
    local out = {}
    for _, word in ipairs(words) do out[#out + 1] = word:sub(1, 1):upper() .. word:sub(2):lower() end
    return table.concat(out)
  end,
  year = function() return os.date("%Y") end,
  date = function() return os.date("%Y-%m-%d") end,
  user = function() return os.getenv("REALNAME") or os.getenv("USER") or "author" end,
  email = function() return os.getenv("EMAIL") or "" end,
  comment_hash = function()
    local out = {}
    for line in (value .. "\n"):gmatch("(.-)\n") do
      local indent, text = line:match("^(%s*)(.*)$")
      if text:match("^#%s?") then
        text = text:gsub("^#%s?", "", 1)
      elseif text ~= "" then
        text = "# " .. text
      end
      out[#out + 1] = indent .. text
    end
    return table.concat(out, "\n")
  end,
  get_type = function() return value:gsub("-", "_"):lower() .. "_get_type" end,
  gobject_macros = function()
    local upper = operations.upper and value:gsub("-", "_"):upper() or value
    local type_name = (function()
      local first, rest = upper:match("^([^_]+)_(.+)$")
      return first and (first .. "_TYPE_" .. rest) or (upper .. "_TYPE")
    end)()
    local is_name = (function()
      local first, rest = upper:match("^([^_]+)_(.+)$")
      return first and (first .. "_IS_" .. rest) or (upper .. "_IS")
    end)()
    local camel = operations.camel()
    return table.concat({
      "#define " .. type_name .. " (" .. value:gsub("-", "_"):lower() .. "_get_type ())",
      "#define " .. upper .. "(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), " .. type_name .. ", " .. camel .. "))",
      "#define " .. upper .. "_CONST(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), " .. type_name .. ", " .. camel .. " const))",
      "#define " .. upper .. "_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), " .. type_name .. ", " .. camel .. "Class))",
      "#define " .. is_name .. "(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), " .. type_name .. "))",
      "#define " .. is_name .. "_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), " .. type_name .. "))",
      "#define " .. upper .. "_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), " .. type_name .. ", " .. camel .. "Class))"
    }, "\n")
  end,
  gobject_typedefs = function()
    local camel = operations.camel()
    return "typedef struct _" .. camel .. " " .. camel .. ";\n" ..
           "typedef struct _" .. camel .. "Class " .. camel .. "Class;\n" ..
           "typedef struct _" .. camel .. "Private " .. camel .. "Private;"
  end,
  ginterface_typedefs = function()
    local camel = operations.camel()
    return "typedef struct _" .. camel .. " " .. camel .. ";\n" ..
           "typedef struct _" .. camel .. "Iface " .. camel .. "Iface;"
  end,
  ginterface_macros = function()
    local upper = value:gsub("-", "_"):upper()
    local first, rest = upper:match("^([^_]+)_(.+)$")
    local type_name = first and (first .. "_TYPE_" .. rest) or (upper .. "_TYPE")
    local is_name = first and (first .. "_IS_" .. rest) or (upper .. "_IS")
    local camel = operations.camel()
    return table.concat({
      "#define " .. type_name .. " (" .. value:gsub("-", "_"):lower() .. "_get_type ())",
      "#define " .. upper .. "(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), " .. type_name .. ", " .. camel .. "))",
      "#define " .. is_name .. "(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), " .. type_name .. "))",
      "#define " .. upper .. "_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), " .. type_name .. ", " .. camel .. "Iface))"
    }, "\n")
  end,
}

local transform = operations[operation]
if not transform then
  io.stderr:write("unsupported snippets operation: " .. tostring(operation) .. "\n")
  os.exit(2)
end

io.write(transform())
