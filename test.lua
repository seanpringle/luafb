-- Copyright (c) 2017 Sean Pringle sean.pringle@gmail.com
--
-- Permission is hereby granted, free of charge, to any person obtaining a
-- copy of this software and associated documentation files (the
-- "Software"), to deal in the Software without restriction, including
-- without limitation the rights to use, copy, modify, merge, publish,
-- distribute, sublicense, and/or sell copies of the Software, and to
-- permit persons to whom the Software is furnished to do so, subject to
-- the following conditions:
--
-- The above copyright notice and this permission notice shall be included
-- in all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
-- OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
-- MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
-- IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
-- CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
-- TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
-- SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

function Class(inherit)
  local o = { }
  o.__index = o
  if inherit then
    setmetatable(o, { __index = inherit })
  end
  o.new = function(self, config)
    local n = { }
    n.__index = n
    setmetatable(n, self)
    n.init(n, config)
    return n
  end
  o.init = function(self, config)
  end
  return o
end

function fb.print(x, y, text)
  fb.blit(x, y, fb.text(text))
end

Box = Class(nil)

function Box:init(config)

  self.parent = config.parent

  if self.parent then
    self.parent:attach(self)
  end

  self.x = config.x or 0
  self.y = config.y or 0
  self.w = config.w or 1.0
  self.h = config.h or 1.0

  self.canvas = fb.canvas(self.w, self.h)

  self.boxes = { }

  self.bg = config.bg or { 0, 0, 0, 0 }
  self.fg = config.fg or { 1, 1, 1, 1 }

  self.refresh = true
end

function Box:attach(child)
  table.insert(self.boxes, child)
end

function Box:detach(child)
  for i, box in ipairs(self.boxes) do
    if box == child then
      table.remove(self.boxes, i)
    end
  end
end

function Box:render()
  fb.print(0, 0, string.format("window %d %d %d %d", self.x, self.y, self.w, self.h))
end

function Box:tick()

  local refresh = self.refresh
  self.refresh = false

  for i, child in ipairs(self.boxes) do
    refresh = child:tick() or refresh
  end

  if refresh then
    fb.push(self.canvas)
    fb.font("sans.ttf", 1.0)
    fb.clear(self.bg[1], self.bg[2], self.bg[3], self.bg[4])
    fb.color(self.fg[1], self.fg[2], self.fg[3], self.fg[4])
    self:render()
    fb.pop()
  end

  return refresh
end

function Box:draw()
  for i, child in ipairs(self.boxes) do
    child:draw()
  end
  if self.parent then
    fb.push(self.parent.canvas)
    fb.blit(self.x, self.y, self.canvas)
    fb.pop()
  else
    fb.render(self.canvas)
  end
end

root = Box:new({
  x = 0,
  y = 0,
  w = 1.0,
  h = 1.0,
  bg = { 0, 0, 0, 1 }
})

last_tick = 0
last_render = 0

while true do

  tick = os.now()

  if tick > last_tick + 1/10 then
    last_tick = tick
    if root:tick() or tick > last_render + 1 then
      root:draw()
    end
  end

  os.sleep(1/100)

end
