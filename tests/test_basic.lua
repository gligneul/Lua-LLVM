-- LLL - Lua Low Level
-- September, 2015
-- Author: Gabriel de Quadros Ligneul
-- Copyright Notice for LLL: see lllvmjit.h
--
-- test_basic.lua

local lll_test = require 'tests/lll_test' 

-- Create all possible combinations of parameters and returns
local params = {'', 'a', 'a, b', 'a, b, c'}
local rets = {'10', '12.3', '"abc"', '"abc", 123', 'function() end', 'nil, 123',
        'a', 'b', 'c', 'c, b', '123, c'}

local functions = {}
for _, p in ipairs(params) do
    for _, r in ipairs(rets) do
        f = 'return function(' .. p .. ') '
        if r ~= '' then
            f = f .. 'return ' .. r .. ' end'
        else
            f = f .. 'end'
        end
        table.insert(functions, f)
    end
end

local args = {
    {},
    {10},
    {12.3},
    {function() end},
    {3, 5.7},
    {nil, 123},
    {'abc', function() end},
}

lll_test(functions, args)

