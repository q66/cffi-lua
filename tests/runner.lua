-- cffi-lua test runner
-- this will set up the environment so that tests can be run

assert(arg and (#arg >= 1), "no arguments given")

-- set up package.path so that require() works

local tests_path = os.getenv("TESTS_PATH")

if not tests_path or (#tests_path == 0) then
    if arg[0] then
        tests_path = arg[0]:match("(.+)[\\/]")
    end
end

assert(tests_path and (#tests_path > 0), "couldn't find tests directory")

local dirsep = "/"
local psep = ";"
local pmatch = "?"

if package.config then
    dirsep, psep, pmatch = package.config:match("([^\n]+)\n([^\n]+)\n([^\n]+)")
elseif package.path:match("\\") then
    -- heuristics for 5.1 and windows
    disep = "\\"
end

local tpn = tests_path:gsub("\\/", dirsep)
if tpn:match(".$") ~= dirsep then
    tpn = tpn .. dirsep
end
package.path = tpn .. "?.lua"

-- set up package.cpath

local tl_path = os.getenv("CFFI_PATH")

if tl_path and (#tl_path > 0) then
    tl_path = tl_path:gsub("\\/", dirsep)
    if tl_path:gmatch(".$") ~= dirsep then
        tl_path = tl_path .. dirsep
    end
    if package.cpath:match("%.dll") then
        package.cpath = tl_path .. "?.dll"
    else
        package.cpath = tl_path .. "?.so"
    end
end

-- test lib

skip_test = function()
    os.exit(77)
end

-- run the testcase

dofile(arg[1])
