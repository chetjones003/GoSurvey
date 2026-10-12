// Converts the GoSurvey.wiki pages into static HTML under docs/, styled like the rest of
// the marketing site. The site now lives in the separate chetjones003/gosurveycad-site
// repo. Re-run this after the wiki changes and redeploy via `wrangler pages deploy`
// (there is no live link to GitHub at request time).
//
// Usage:
//   git clone --depth 1 https://github.com/chetjones003/GoSurvey.wiki.git <tmp>
//   node generate.js <tmp> <path-to-gosurveycad-site>/docs

const fs = require("fs");
const path = require("path");
const { marked } = require("marked");

const [, , wikiDirArg, outDirArg] = process.argv;
if (!wikiDirArg || !outDirArg) {
  console.error("usage: node generate.js <wiki-dir> <out-dir>");
  process.exit(1);
}
const wikiDir = path.resolve(wikiDirArg);
const outDir = path.resolve(outDirArg);

// Sidebar groups, in wiki page-title form -> mirrors the wiki's own _Sidebar.md.
const GROUPS = [
  { title: "Start", pages: ["Getting Started", "User Interface", "Command Reference", "Keyboard Shortcuts"] },
  { title: "Draw & Edit", pages: ["Drawing Tools", "Modify Tools", "Object Selection", "Object Snaps", "Coordinate Input", "Drafting Aids", "Annotation", "Layers", "Properties"] },
  { title: "Survey", pages: ["Survey Points", "Point Groups and Surfaces", "Traverse Editor", "Coordinate Alignment", "Inquiry Commands"] },
  { title: "Output", pages: ["Views and Navigation", "Paper Space and Layouts", "Plotting", "PDF Underlays", "Import and Export", "Files and Drawings"] },
  { title: "Reference", pages: ["Settings and Options", "Command Line", "Workflows", "Troubleshooting", "FAQ", "Known Limitations"] },
];

function slugify(title) {
  return title.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/(^-|-$)/g, "");
}
function titleToFilename(title) {
  return title.replace(/ /g, "-") + ".md"; // wiki convention: "Getting Started" -> Getting-Started.md
}

const allTitles = ["Home", ...GROUPS.flatMap((g) => g.pages)];
const slugOf = {};
allTitles.forEach((t) => (slugOf[t] = t === "Home" ? "index" : slugify(t)));

function wikiLinkToHref(title) {
  const slug = slugOf[title];
  return slug ? slug + ".html" : null;
}

// Rewrite [[Page Name]] and [[Label|Page Name]] wiki links to relative hrefs.
function rewriteWikiLinks(md) {
  return md.replace(/\[\[([^\]]+)\]\]/g, (_, inner) => {
    const parts = inner.split("|");
    const label = parts.length > 1 ? parts[0] : parts[0];
    const target = parts.length > 1 ? parts[1] : parts[0];
    const href = wikiLinkToHref(target.trim());
    if (!href) return label;
    return `[${label}](${href})`;
  });
}

function renderPage({ title, bodyHtml, prevLink, nextLink }) {
  const navHtml = GROUPS.map((g) => {
    const items = g.pages
      .map((p) => {
        const href = wikiLinkToHref(p);
        const current = p === title ? ' aria-current="page"' : "";
        return `<li><a href="${href}"${current}>${p}</a></li>`;
      })
      .join("\n        ");
    return `<div class="doc-nav-group">\n        <span class="doc-nav-head">${g.title}</span>\n        <ul>\n        ${items}\n        </ul>\n      </div>`;
  }).join("\n      ");

  const pager = `<div class="doc-pager">${
    prevLink ? `<a class="doc-pager-link prev" href="${prevLink.href}">&larr; ${prevLink.title}</a>` : "<span></span>"
  }${nextLink ? `<a class="doc-pager-link next" href="${nextLink.href}">${nextLink.title} &rarr;</a>` : ""}</div>`;

  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${title} — GoSurvey Docs</title>
<meta name="description" content="GoSurvey documentation: ${title}.">
<link rel="icon" href="../favicon.ico">
<link rel="canonical" href="https://gosurveycad.com/docs/${slugOf[title]}.html">

<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans+Condensed:wght@500;600;700&family=IBM+Plex+Sans:wght@400;500;600&display=swap" rel="stylesheet">
<link rel="stylesheet" href="../styles.css">
<link rel="stylesheet" href="docs.css">
</head>
<body>

<header class="masthead">
  <div class="wrap">
    <a class="brand" href="../index.html">GoSurvey<span class="tick">·</span></a>
    <nav class="nav">
      <a href="../index.html#features" class="hide-sm">Features</a>
      <a href="../index.html#downloads">Download</a>
      <a href="index.html">Docs</a>
      <a href="../about.html">About</a>
      <a href="../contact.html">Contact</a>
      <a href="../account.html">Account</a>
      <a href="https://github.com/chetjones003/GoSurvey">GitHub</a>
    </nav>
  </div>
</header>

<main class="doc-shell wrap">
  <nav class="doc-nav" aria-label="Documentation">
    <a class="doc-nav-home" href="index.html">GoSurvey Manual</a>
    ${navHtml}
  </nav>
  <article class="doc-content">
    <h1>${title}</h1>
    ${bodyHtml}
    ${pager}
  </article>
</main>

<footer>
  <div class="wrap">
    <span>GoSurvey — built by Chet Jones</span>
    <span class="right">
      <a href="https://github.com/chetjones003/GoSurvey">Source</a>
      <a href="https://github.com/chetjones003/GoSurvey/issues">Report an issue</a>
      <a href="https://github.com/chetjones003/GoSurvey/releases">All releases</a>
    </span>
  </div>
</footer>

</body>
</html>
`;
}

marked.use({
  renderer: {
    image(token) {
      const href = token.href || "";
      const text = token.text || "";
      // Wiki images live in images/*.png relative to the wiki root; mirrored into docs/images/.
      const src = /^https?:\/\//.test(href) ? href : "images/" + href.replace(/^\.?\/?/, "");
      return `<img src="${src}" alt="${text}" loading="lazy">`;
    },
  },
});

fs.mkdirSync(outDir, { recursive: true });
fs.mkdirSync(path.join(outDir, "images"), { recursive: true });

const srcImages = path.join(wikiDir, "images");
if (fs.existsSync(srcImages)) {
  for (const f of fs.readdirSync(srcImages)) {
    fs.copyFileSync(path.join(srcImages, f), path.join(outDir, "images", f));
  }
}

const flat = allTitles.map((title, i) => ({ title, href: wikiLinkToHref(title) + "" }));

function buildBody(title) {
  const file = path.join(wikiDir, titleToFilename(title));
  if (!fs.existsSync(file)) {
    console.warn("missing wiki page for", title, "->", file);
    return null;
  }
  const raw = fs.readFileSync(file, "utf8");
  const rewritten = rewriteWikiLinks(raw);
  return marked.parse(rewritten);
}

// Home page (index.html) lists every section, acting as the docs landing page.
const homeBody = buildBody("Home");
if (homeBody !== null) {
  fs.writeFileSync(
    path.join(outDir, "index.html"),
    renderPage({ title: "Home", bodyHtml: homeBody })
  );
}

allTitles
  .filter((t) => t !== "Home")
  .forEach((title, i, arr) => {
    const bodyHtml = buildBody(title);
    if (bodyHtml === null) return;
    const idx = flat.findIndex((f) => f.title === title);
    const prevLink = idx > 1 ? flat[idx - 1] : { title: "Home", href: "index.html" };
    const nextLink = idx < flat.length - 1 ? flat[idx + 1] : null;
    fs.writeFileSync(
      path.join(outDir, slugOf[title] + ".html"),
      renderPage({ title, bodyHtml, prevLink, nextLink })
    );
  });

console.log("Wrote docs to", outDir);
