#!/usr/bin/lua
--============================================================================
-- bin2carray.lua 
--
-- License: GPLv2 (see COPYING) or any later version
--
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
-- Transforms a binary file (the compiled lsyncd runner script) in a c array
-- so it can be included into the executable in a portable way.
--============================================================================

if #arg < 3 then
	error("Usage: "..arg[0].." [infile] [varname] [outfile]") 
end

fin, err = io.open(arg[1], "rb")
if fin == nil then
	error("Cannot open '"..arg[1].."' for reading: "..err)
end

fout, err = io.open(arg[3], "w")
if fout == nil then
	error("Cannot open '"..arg[3].."'for writing: "..err)
end

fout:write("/* created by "..arg[0].." from file "..arg[1].." */\n")
fout:write("#include <stddef.h>\n")
fout:write("const char "..arg[2].."_out[] = {\n")
while true do
	local block = fin:read(16)
	if block == nil then
		break
	end
	for i = 1, #block do
		local val = string.format("%x", block:byte(i))
		if #val < 2 then 
			val = "0" ..val
		end
		fout:write("0x",val,",")
	end
	fout:write("\n")
end
fout:write("};\n\nsize_t "..arg[2].."_size = sizeof("..arg[2].."_out);\n");

fin:close();
fout:close();
