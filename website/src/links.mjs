// Fill project destinations here. Placeholders are deliberately not visitor-facing copy.
export const links={
 github:'https://github.com/cccat6/WishOS', docs:'https://github.com/cccat6/WishOS/blob/main/docs/getting-started.md', quickstart:'https://github.com/cccat6/WishOS/blob/main/docs/getting-started.md', applications:'https://github.com/cccat6/WishOS/tree/main/examples/agent-os', automation:'https://github.com/cccat6/WishOS/blob/main/skills/agentic-notes-hardware/references/runtime-apps.md',
 hardwareDocs:'https://github.com/cccat6/WishOS/blob/main/docs/supported-devices.md', officialMcp:'https://github.com/cccat6/WishOS-Hub', apiSetup:'https://github.com/cccat6/WishOS-Hub', mcpDocs:'https://github.com/cccat6/WishOS-Hub', serverInstall:'https://github.com/cccat6/WishOS-Hub', firmware:'https://github.com/cccat6/WishOS/releases',
 makerworldCN:null, makerworldGlobal:null, oshwhubCN:null, oshwlabGlobal:null
};
export function linkAttrs(key){
 if(!(key in links))throw new Error(`Unknown link placeholder: ${key}`);
 const href=links[key];
 if(!href)return `href="#" data-link-placeholder="${key}"`;
 const url=new URL(href);if(url.protocol!=='https:')throw new Error('Project links must use HTTPS');
 return `href="${url.href.replaceAll('&','&amp;').replaceAll('"','&quot;')}" target="_blank" rel="noopener noreferrer"`;
}
