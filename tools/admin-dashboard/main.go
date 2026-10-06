package main

import (
	"embed"
	"html/template"
	"io/fs"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/chetjones003/GoSurvey/tools/admin-dashboard/internal/handlers"
	"github.com/chetjones003/GoSurvey/tools/admin-dashboard/internal/store"
)

//go:embed templates/*.html templates/partials/*.html
var tmplFS embed.FS

//go:embed static/*
var staticFS embed.FS

var pageNames = []string{"overview", "telemetry", "accounts", "analytics"}

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "8080"
	}

	// Parse every template once. Each page gets its own clone of the shared
	// partials + layout so its {{define "content"}} can't collide with another's.
	partials, err := template.New("").Funcs(handlers.FuncMap).ParseFS(tmplFS, "templates/partials/*.html")
	if err != nil {
		log.Fatalf("parse partials: %v", err)
	}
	pages := map[string]*template.Template{}
	for _, name := range pageNames {
		t, err := partials.Clone()
		if err == nil {
			t, err = t.ParseFS(tmplFS, "templates/layout.html", "templates/"+name+".html")
		}
		if err != nil {
			log.Fatalf("parse page %s: %v", name, err)
		}
		pages[name] = t
	}

	s := store.New()
	h := &handlers.Handlers{
		Store: s, Pages: pages, Partials: partials,
		Asset: strconv.FormatInt(time.Now().Unix(), 36),
	}

	mux := http.NewServeMux()
	staticContent, _ := fs.Sub(staticFS, "static")
	static := http.StripPrefix("/static/", http.FileServer(http.FS(staticContent)))
	mux.Handle("/static/", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// URLs carry ?v=<start time>, so a long cache is safe and saves a round trip per asset.
		w.Header().Set("Cache-Control", "public, max-age=86400")
		static.ServeHTTP(w, r)
	}))

	mux.HandleFunc("/", h.Overview)
	mux.HandleFunc("/telemetry", h.TelemetryPage)
	mux.HandleFunc("/accounts", h.AccountsPage)
	mux.HandleFunc("/analytics", h.AnalyticsPage)
	mux.HandleFunc("/partials/pings", h.PartialPings)
	mux.HandleFunc("/partials/users", h.PartialUsers)
	mux.HandleFunc("/partials/stats", h.PartialStats)
	mux.HandleFunc("/api/pings", h.APIPings)
	mux.HandleFunc("/api/users", h.APIUsers)
	mux.HandleFunc("/api/stats", h.APIStats)
	mux.HandleFunc("/api/health", h.APIHealth)
	mux.HandleFunc("/api/refresh", h.Refresh)

	log.Printf("GoSurvey Admin Dashboard on http://localhost:%s (live=%v)", port, store.LiveEnabled())
	if store.LiveEnabled() {
		log.Printf("  fetching D1 in the background; showing mock data until it answers")
	}
	log.Printf("  Pages: /  /telemetry  /accounts  /analytics")
	log.Printf("  API:   /api/stats  /api/pings  /api/users  /api/health")
	if err := http.ListenAndServe(":"+port, withLogging(mux)); err != nil {
		log.Fatal(err)
	}
}

func withLogging(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.HasPrefix(r.URL.Path, "/static/") {
			next.ServeHTTP(w, r)
			return
		}
		start := time.Now()
		next.ServeHTTP(w, r)
		log.Printf("%s %s %s", r.Method, r.URL.Path, time.Since(start).Round(time.Millisecond))
	})
}
