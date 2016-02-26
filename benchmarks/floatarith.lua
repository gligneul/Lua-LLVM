-- LLL - Lua Low Level
-- September, 2015
-- Author: Gabriel de Quadros Ligneul
-- Copyright Notice for LLL: see lllcore.h

local benchmark_util = require 'benchmarks/util'

benchmark_util(function()

local a = 123.1
for i = 1, 1e8 do
    a = a * 1.1
end

end)
