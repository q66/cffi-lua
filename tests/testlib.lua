local ffi = require("cffi")

local tlp = os.getenv("TESTLIB_PATH")
if not tlp or (#tlp == 0) then
    skip_test()
end

return ffi.load(tlp)
