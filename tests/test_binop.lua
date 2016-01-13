-- LLL - Lua Low Level
-- September, 2015
-- Author: Gabriel de Quadros Ligneul
-- Copyright Notice for LLL: see lllvmjit.h
--
-- test_binop.lua

local executetests = require 'tests/executetests' 

-- Create all possible combinations of operations and values
local ops = {'+', '-', '*', '/', '&', '|', '~', '<<', '>>', '%', '//', '^',
             '..'}
local values = {'nil', 'true', 'false', '-5000', '0', '123', '0.00001', '1.23',
        '10e999', '"0"', '"15.4"', '"a"'}

local functions = {}
for _, op in ipairs(ops) do
    for _, v1 in ipairs(values) do
        for _, v2 in ipairs(values) do
            f_rr = 'return function() local a, b = ' .. v1 .. ', ' .. v2
                    .. ' return a ' .. op .. ' b end'
            table.insert(functions, f_rr)

            f_rk = 'return function() local a = ' .. v1
                    .. ' return a ' .. op .. ' ' .. v2 .. ' end'
            table.insert(functions, f_rk)

            f_kr = 'return function() local b = ' .. v2
                    .. ' return ' .. v1 .. ' ' .. op .. ' b end'
            table.insert(functions, f_kr)

            f_kk = 'return function() '
                    .. 'return ' .. v1 .. ' ' .. op .. ' ' .. v2 .. ' end'
            table.insert(functions, f_kk)
        end
    end
end

executetests(functions, {{}})
