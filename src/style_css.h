// style_css.h

#pragma once

const char* cssContent = R"rawliteral(
:root{
      --header:#2c3e50; --side:#34495e; --bg:#f5f5f5; --text:#333;
      --border:#dddddd; --muted:#ffffff; --link:#2c3e50;
      --sidebar-w:200px; --radius:10px;
    }
    :root[data-theme='dark']{
      --header:#111; --side:#1b1b1b; --bg:#121212; --text:#eaeaea;
      --border:#2a2a2a; --muted:#1f1f1f; --link:#9ec1ff;
      --primary-hover: #1e40af;
    }
    .hidden { display:none; }
    *{box-sizing:border-box}
    html{font-size:16px}
    body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;background:var(--bg);color:var(--text);line-height:1.45}
    a{color:var(--link)}
    h1{font-size:clamp(1.25rem,1.2vw+1rem,1.8rem);margin:0 0 .75rem}
    p,label,input,select,button{font-size:clamp(.95rem,.4vw+.85rem,1.05rem)}

    /* Header */
    .header{
      position:sticky;top:0;display:flex;align-items:center;justify-content:space-between;
      gap:.75rem;height:56px;padding:0 16px;background:var(--header);color:#fff;z-index:50
    }
    .hamburger{inline-size:40px;block-size:40px;display:inline-flex;align-items:center;justify-content:center;background:transparent;border:0;color:inherit;cursor:pointer;border-radius:var(--radius);font-size:22px}
    .hamburger:focus-visible{outline:2px solid #fff;outline-offset:2px}
    .title{font-weight:600;font-size:clamp(1rem,.6vw+.9rem,1.2rem)}
    .datetime{display:flex;flex-direction:column;align-items:flex-end;font-size:.85rem;line-height:1.2}

    .dirty-hint{
      margin-left:8px;
      background:#ffdf91;
      color:#222;
      padding:3px 10px;
      border-radius:999px;
      font-size:.8rem;
      font-weight:600;
      display:inline-flex;
      align-items:center;
      gap:.4rem;
      box-shadow:0 1px 3px rgba(0,0,0,.15);
    }

    .dirty-hint::before{ content:"⚠️"; }
    :root[data-theme="dark"] .dirty-hint{
      background:#3d320e; color:#ffe9b0;
    }

    /* Sidebar / Overlay */
    .sidebar{position:fixed;inset:0 auto 0 0;inline-size:var(--sidebar-w);background:var(--side);color:#fff;transform:translateX(-100%);transition:transform .3s ease;padding-top:62px;z-index:40}
    .sidebar--open{transform:translateX(0)}
    .navlink{display:block;padding:12px 18px;text-decoration:none;color:#fff}
    .navlink:hover{background:rgba(255,255,255,.08)}
    .navlink[aria-current='page']{background:rgba(255,255,255,.12)}
    .overlay{position:fixed;inset:0;background:rgba(0,0,0,.35);opacity:0;visibility:hidden;pointer-events:none;transition:opacity .3s ease,visibility 0s linear .3s;z-index:20}
    .overlay--show{opacity:1;visibility:visible;pointer-events:auto;transition-delay:0s}

    /* Content / Layout */
    .content{padding:clamp(14px,1.5vw,24px)}
    .page{display:none;animation:fade .2s}
    .page.active{display:block}
    @keyframes fade{from{opacity:0}to{opacity:1}}
    .form-group{margin-block:0 14px}
    label{display:block;margin-block-end:6px}
    input,select{width:100%;padding:10px;border:1px solid var(--border);border-radius:var(--radius);background:var(--muted);color:var(--text)}
    /* button.primary{width:100%;padding:10px;border:0;border-radius:var(--radius);background:var(--header);color:#fff;cursor:pointer} */
    button.primary {
      width: 100%;
      padding: 10px;
      border: 0;
      border-radius: var(--radius);
      background: var(--header);
      color: #fff;
      cursor: pointer;
      transition: all 0.25s ease;          /* sanfter Übergang */
      box-shadow: 0 2px 6px rgba(0,0,0,0.15);
    }

    button.primary:hover {
      transform: translateY(-2px);         /* leichter „Lift“-Effekt */
      box-shadow: 0 4px 10px rgba(0,0,0,0.25);
      filter: brightness(1.05);            /* etwas heller */
    }

    button.primary:active {
      transform: translateY(0);            /* wieder runter beim Klick */
      box-shadow: 0 2px 6px rgba(0,0,0,0.15);
      filter: brightness(.95);
    }

    @media (min-width:1024px){
      .hamburger{display:none}
      .sidebar{transform:none;position:sticky;inset:auto;top:56px;height:calc(100dvh - 56px)}
      .layout{display:grid;grid-template-columns:var(--sidebar-w) 1fr;min-height:calc(100dvh - 56px)}
      .overlay{display:none}
      .content{padding:clamp(18px,2vw,32px)}
    }

    /* Card */
    .card{background:var(--muted);border:1px solid var(--border);border-radius:var(--radius);padding:16px}

    /* Gauge layout (no external CSS) */
    .gauges-grid{display:grid;gap:16px;grid-template-columns:repeat(auto-fit, minmax(240px,1fr));align-items:start}
    .gauge-card{border:1px solid var(--border);border-radius:var(--radius);padding:14px;background:var(--muted)}
    .g-title{font-weight:700;margin-bottom:6px}
    .gauge{width:100%;max-width:360px;margin:auto}
    .g-arc{stroke:var(--border);stroke-width:8;fill:none;stroke-linecap:round}
    .g-needle{stroke:var(--header);stroke-width:2}
    .g-pivot{fill:var(--header)}
    .g-label{text-align:center;margin-top:6px;font-weight:700}
    .g-minmax{display:flex;justify-content:space-between;font-size:.85rem;opacity:.85;margin-top:4px}

)rawliteral";