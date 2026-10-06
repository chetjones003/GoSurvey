package main

import (
	"html/template"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/chetjones003/GoSurvey/tools/admin-dashboard/internal/handlers"
	"github.com/chetjones003/GoSurvey/tools/admin-dashboard/internal/store"
)

// TestPagesRender catches template wiring mistakes (undefined templates, bad
// field names) that only surface at execute time.
func TestPagesRender(t *testing.T) {
	partials, err := template.New("").Funcs(handlers.FuncMap).ParseFS(tmplFS, "templates/partials/*.html")
	if err != nil {
		t.Fatal(err)
	}
	pages := map[string]*template.Template{}
	for _, n := range pageNames {
		c, _ := partials.Clone()
		if pages[n], err = c.ParseFS(tmplFS, "templates/layout.html", "templates/"+n+".html"); err != nil {
			t.Fatal(err)
		}
	}
	h := &handlers.Handlers{Store: store.NewMock(), Pages: pages, Partials: partials, Asset: "t"}
	routes := map[string]http.HandlerFunc{
		"/": h.Overview, "/telemetry": h.TelemetryPage, "/accounts": h.AccountsPage, "/analytics": h.AnalyticsPage,
		"/partials/pings?page=2": h.PartialPings, "/partials/users?tier=pro": h.PartialUsers, "/partials/stats": h.PartialStats,
	}
	for path, fn := range routes {
		rec := httptest.NewRecorder()
		fn(rec, httptest.NewRequest("GET", path, nil))
		if rec.Code != 200 || rec.Body.Len() == 0 {
			t.Errorf("%s: status %d, %d bytes: %s", path, rec.Code, rec.Body.Len(), rec.Body.String())
		}
	}
}
