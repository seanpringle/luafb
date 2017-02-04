-- drawing canvas covering entire screen
canvas = fb.canvas(1.0, 1.0)

-- set default font to sans-serif
fb.font('sans.ttf', 1.0)

-- print text at x,y co-ordinates
function fb.print(x, y, text)
  fb.blit(x, y, fb.text(text))
end

-- a black pen
function black(alpha)
  return 0.0, 0.0, 0.0, alpha or 1.0
end

-- a white pen
function white(alpha)
  return 1.0, 1.0, 1.0, alpha or 1.0
end

opaque = 1.0
transparent = 0.0
same = -1

going = true

while going do

  -- start drawing on our canvas
  fb.push(canvas)

  -- set the background color to black
  fb.clear(black(opaque))
  
  -- set the drawing color to white
  fb.color(white(opaque))

  -- display some white text at x=0,y=0
  fb.print(0, 0, 'hello world')
  
  -- draw a white box 
  fb.box(0.25, 0.25, 0.1, same)

  -- draw a white line
  fb.line(0.5, 0.5, 0.6, 0.6)

  -- finish drawing on our canvas
  fb.pop()

  -- copy our canvas to the screen
  fb.render(canvas)

  -- read one keyboard key
  local key = io.read(1)

  -- press 'q' to stop the program
  if key == 'q' then
    going = false
  end
end