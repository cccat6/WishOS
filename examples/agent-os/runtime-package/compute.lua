-- Self-contained, isolated computation: no driver calls or shared mutable state.
function compute(input)
  local sum=0
  for i=1,input.count do sum=sum+i end
  return {sum=sum}
end
