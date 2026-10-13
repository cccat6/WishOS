local page={}
function page.result(value)
  return {version='0.1.1',assets={{type='viewScreen',id='result',
    layout={type='flex',flexFlow='column',gap='12dp'},
    style={bgColor='#ffffff',padding='24dp'},children={
      {type='label',id='title',labelProps={text='Local computation'},style={fontSize='32sp',textColor='#0000ff'}},
      {type='label',id='value',labelProps={text=tostring(value)},style={fontSize='40sp',textColor='#000000'}}
    }}}}
end
return page
