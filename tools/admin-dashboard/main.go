package main

import (
	"bytes"
	"embed"
	"encoding/json"
	"html/template"
	"io/fs"
	"log"
	"net/http"
	"os"
	"strings"

	"github.com/chetjones003/GoSurvey/tools/admin-dashboard/internal/handlers"
	"github.com/chetjones003/GoSurvey/tools/admin-dashboard/internal/store"
)

//go:embed templates/*.html templates/partials/*.html
var tmplFS embed.FS

//go:embed static/*
var staticFS embed.FS

var funcMap = template.FuncMap{
	"toJSON": func(v any) template.JS {
		b, _ := json.Marshal(v)
		return template.JS(b)
	},
	"percent": func(n, denom int) int {
		if denom == 0 {
			return 0
		}
		return n * 100 / denom
	},
}

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "8080"
	}
	s := store.New()

	// Pre-parse layout and partials for HTMX/API
	baseTmpl := template.New("").Funcs(funcMap)
	if _, err := baseTmpl.ParseFS(tmplFS, "templates/partials/*.html"); err != nil {
		log.Fatalf("parse partials: %v", err)
	}
	layoutBytes, err := fs.ReadFile(tmplFS, "templates/layout.html")
	if err != nil {
		log.Fatalf("read layout: %v", err)
	}
	layoutTmpl, err := template.New("layout").Funcs(funcMap).Parse(string(layoutBytes))
	if err != nil {
		log.Fatalf("parse layout: %v", err)
	}

	h := &handlers.Handlers{Store: s, Tmpl: baseTmpl}

	mux := http.NewServeMux()
	staticContent, _ := fs.Sub(staticFS, "static")
	mux.Handle("/static/", http.StripPrefix("/static/", http.FileServer(http.FS(staticContent))))

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		renderPage(w, layoutTmpl, "overview.html", map[string]any{
			"Title": "Overview", "PageTitle": "Overview",
			"PageSub":   "Installs, activity and versions — last 30 days. Source: gosurvey-telemetry pings + gosurvey-accounts users.",
			"ActivePage": "overview", "Mode": s.Mode(), "LiveError": store.LiveError(),
			"Stats": s.ComputeStats(), "Pings": s.AllPings(), "Users": s.AllUsers(),
		})
	})
	mux.HandleFunc("/telemetry", func(w http.ResponseWriter, r *http.Request) {
		renderPage(w, layoutTmpl, "telemetry.html", map[string]any{
			"Title": "Telemetry", "PageTitle": "Telemetry — pings",
			"PageSub":   "gosurvey-telemetry D1 · table pings · live via wrangler D1 execute",
			"ActivePage": "telemetry", "Mode": s.Mode(), "LiveError": store.LiveError(),
			"Versions": store.DistinctVersions(s.AllPings()), "Stats": s.ComputeStats(),
		})
	})
	mux.HandleFunc("/accounts", func(w http.ResponseWriter, r *http.Request) {
		renderPage(w, layoutTmpl, "accounts.html", map[string]any{
			"Title": "Accounts", "PageTitle": "Accounts — users",
			"PageSub":   "gosurvey-accounts D1 · table users (auth0_sub PK, email, tier, created_at) · JWT-verified worker",
			"ActivePage": "accounts", "Mode": s.Mode(), "LiveError": store.LiveError(), "Stats": s.ComputeStats(),
		})
	})
	mux.HandleFunc("/analytics", func(w http.ResponseWriter, r *http.Request) {
		renderPage(w, layoutTmpl, "analytics.html", map[string]any{
			"Title": "Analytics", "PageTitle": "Analytics",
			"PageSub":   "Shipped queries from tools/telemetry-worker/queries.sql — totals, DAU, version/channel, retention, geography",
			"ActivePage": "analytics", "Mode": s.Mode(), "LiveError": store.LiveError(), "Stats": s.ComputeStats(),
		})
	})

	mux.HandleFunc("/partials/pings", h.PartialPings)
	mux.HandleFunc("/partials/users", h.PartialUsers)
	mux.HandleFunc("/partials/stats", h.PartialStats)
	mux.HandleFunc("/api/pings", h.APIPings)
	mux.HandleFunc("/api/users", h.APIUsers)
	mux.HandleFunc("/api/stats", h.APIStats)
	mux.HandleFunc("/api/health", h.APIHealth)
	mux.HandleFunc("/api/refresh", func(w http.ResponseWriter, r *http.Request) {
		if r.Method == "POST" {
			if err := s.RefreshLive(); err != nil {
				http.Error(w, err.Error(), 500)
				return
			}
		} else {
			s.EnsureLive()
		}
		http.Redirect(w, r, r.Referer(), http.StatusSeeOther)
	})
	mux.HandleFunc("/api/live-status", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]any{"mode": s.Mode(), "liveEnabled": store.LiveEnabled(), "error": store.LiveError()})
	})

	log.Printf("GoSurvey Admin Dashboard on http://localhost:%s (mode=%s live=%v err=%q)", port, s.Mode(), store.LiveEnabled(), store.LiveError())
	log.Printf("  Pages: /  /telemetry  /accounts  /analytics")
	log.Printf("  API:   /api/stats  /api/pings  /api/users  /api/health")
	if err := http.ListenAndServe(":"+port, withLogging(mux)); err != nil {
		log.Fatal(err)
	}
}

func renderPage(w http.ResponseWriter, layout *template.Template, pageFile string, data map[string]any) {
	// Render page content fragment first
	pageBytes, err := fs.ReadFile(tmplFS, "templates/"+pageFile)
	if err != nil {
		http.Error(w, "missing template "+pageFile, 500)
		return
	}
	pageTmpl, err := template.New(pageFile).Funcs(funcMap).Parse(string(pageBytes))
	if err != nil {
		http.Error(w, "parse "+pageFile+": "+err.Error(), 500)
		return
	}
	var buf bytes.Buffer
	if err := pageTmpl.Execute(&buf, data); err != nil {
		http.Error(w, "execute "+pageFile+": "+err.Error(), 500)
		return
	}
	// Inject into layout
	data["Content"] = template.HTML(buf.String())
	// Map Title/PageTitle for layout
	if _, ok := data["Title"]; !ok {
		data["Title"] = data["PageTitle"]
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	if err := layout.Execute(w, data); err != nil {
		http.Error(w, err.Error(), 500)
	}
}

func withLogging(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.HasPrefix(r.URL.Path, "/static/") {
			next.ServeHTTP(w, r)
			return
		}
		log.Printf("%s %s", r.Method, r.URL.Path)
		next.ServeHTTP(w, r)
	})
}
