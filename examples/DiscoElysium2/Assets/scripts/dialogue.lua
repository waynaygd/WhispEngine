-- Data-driven graph interpreter. No authored dialogue text lives in code.
local M = {}
-- Runtime Text supports explicit newlines. Wrap by UTF-8 characters, preserving
-- authored paragraphs without splitting Cyrillic byte sequences.
function M.wrap(text, width)
    local result = {}
    for paragraph in (text..'\n'):gmatch('(.-)\n') do
        local line, length = '', 0
        for word in paragraph:gmatch('%S+') do
            local n = utf8.len(word)
            if length > 0 and length + 1 + n > width then
                result[#result+1] = line
                line, length = '', 0
            end
            if length > 0 then line = line..' ';length = length+1 end
            line, length = line..word, length+n
        end
        result[#result+1] = line
    end
    return table.concat(result,'\n')
end
function M.prepare(graph)
    local nodes = {}
    for _, n in ipairs(graph.nodes) do
        assert(n.id ~= '' and not nodes[n.id], 'Duplicate dialogue node ID')
        nodes[n.id] = n
    end
    assert(nodes[graph.start], 'Missing dialogue entry')
    for _, n in ipairs(graph.nodes) do

        for _, c in ipairs(n.choices) do
            for _, target in ipairs({c.next, c.success, c.failure}) do
                assert(target == '' or nodes[target], 'Missing dialogue target: '..target)
            end
        end
    end
    return {nodes=nodes, current=graph.start}
end
function M.choices(dialogue, flags)
    local result = {}
    for _, c in ipairs(dialogue.nodes[dialogue.current].choices) do
        if (c.requires == '' or flags[c.requires]) and (c.excludes == '' or not flags[c.excludes]) then
            result[#result+1] = c
        end
    end
    return result
end
function M.choose(dialogue, choice, state, roll)
    if not choice then return false end
    if choice.requires ~= '' and not state.flags[choice.requires] then return false end
    if choice.excludes ~= '' and state.flags[choice.excludes] then return false end
    if choice.flag ~= '' then state.flags[choice.flag] = true end
    if choice.quest ~= '' then state.quest = choice.quest end
    local target = choice.next
    state.feedback = ''
    if choice.check ~= '' then
        local check = Data.find('data/checks.table.json', choice.check)
        assert(check, 'Unknown skill check')
        local skill = Data.find('data/skills.table.json', check.skill)
        assert(skill, 'Unknown skill')
        local difficulty = choice.difficulty > 0 and choice.difficulty or check.difficulty
        local bonus = state.flags[check.bonusFlag] and check.bonus or 0
        local dice = roll or (math.random(1,6) + math.random(1,6))
        local total = dice + skill.value + bonus
        local success = total >= difficulty
        state.feedback = skill.name..': '..dice..' + '..skill.value..' + '..bonus..' = '..total..' / '..difficulty..(success and ' — успех' or ' — неудача')
        target = success and choice.success or choice.failure
    end
    if target == '' then return false end
    assert(dialogue.nodes[target], 'Invalid dialogue transition')
    dialogue.current = target
    return true
end
return M
