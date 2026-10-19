# WishOS 官网

中英文产品介绍、设备支持、入门文档和浏览器交互示例。静态页面部署到 Cloudflare Workers，生产域名为 wishos.dev。

```sh
npm ci
npm run check
npm run dev
# 发布到自己的 Cloudflare 账号
npm run deploy
```

`src/` 保存页面内容，`public/` 保存浏览器资源，`assets/` 保存品牌图形。`scripts/build.mjs` 生成 `dist/`，静态打包器将预压缩资源与路由生成一份 Worker 模块，可通过 Cloudflare API 或 Wrangler 发布。部署时按自己的域名修改打包器中的路由。
