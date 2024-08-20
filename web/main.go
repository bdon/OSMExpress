package main

import (
	"bufio"
	"bytes"
	_ "embed"
	"encoding/json"
	"flag"
	"fmt"
	"github.com/getsentry/sentry-go"
	"github.com/google/uuid"
	"github.com/paulmach/orb"
	"github.com/paulmach/orb/geojson"
	"github.com/paulmach/orb/maptile"
	"github.com/paulmach/orb/maptile/tilecover"
	"image"
	"image/png"
	"io"
	"io/ioutil"
	"log"
	"math"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"
)

// used to estimate the nodes count on the client
// as well as enforce it on the server.
//
//go:embed z12_red_green.png
var imageBytes []byte

// global system state.
type SystemState struct {
	QueueSize  int
	NodesLimit int
	Timestamp  string
}

// the content of a POST request
type Input struct {
	Name       string
	RegionType string // geojson, bbox
	RegionData json.RawMessage
}

// A sanitized serialization of the submitted job
// used for the UI to display region, to re-create the region,
// and to name the download.
type Task struct {
	Uuid                string
	SanitizedName       string
	SanitizedRegionType string
	SanitizedRegionData json.RawMessage
}

// Used to display progress. When complete, is persisted
// on the filesystem but still served through /api endpoint.
type Progress struct {
	// fields that come from osmx
	Timestamp  string
	CellsTotal int64
	CellsProg  int64
	NodesTotal int64
	NodesProg  int64
	ElemsTotal int64
	ElemsProg  int64

	SizeBytes int64
	Elapsed   float64
	Complete  bool
}

type server struct {
	progress      map[string]Progress
	progressMutex sync.RWMutex
	queue         chan Task
	resultsDir    string
	tmpDir        string
	osmxExec      string
	osmxData      string
	image         image.Image
	nodesLimit    int

	timestampMutex    sync.Mutex
	lastTimestampTime time.Time
	lastTimestamp     string
}

func (h *server) worker(id int, queue chan Task) {
	for task := range queue {
		h.progressMutex.Lock()
		h.progress[task.Uuid] = Progress{}
		h.progressMutex.Unlock()

		uuid := task.Uuid

		fmt.Println("worker", id, "started job", uuid)
		start := time.Now()

		pbfPath := filepath.Join(h.tmpDir, uuid+".osm.pbf")

		regionPath := filepath.Join(h.tmpDir, uuid+"."+task.SanitizedRegionType)

		out, err := os.Create(regionPath)
		if err != nil {
			log.Fatal(err)
		}
		if task.SanitizedRegionType == "bbox" {
			out.Write(task.SanitizedRegionData[1 : len(task.SanitizedRegionData)-1])
		} else {
			out.Write(task.SanitizedRegionData)
		}
		out.Close()

		task_json, _ := json.Marshal(task)
		err = ioutil.WriteFile(filepath.Join(h.resultsDir, uuid+"_region.json"), task_json, 0644)

		args := []string{"extract", h.osmxData, pbfPath, "--jsonOutput", "--noUserData", "--region", regionPath}
		cmd := exec.Command(h.osmxExec, args...)
		stdout, err := cmd.StdoutPipe()

		err = cmd.Start()
		if err != nil {
			log.Fatal(err)
		}
		reader := bufio.NewReader(stdout)
		line, err := reader.ReadString('\n')
		for err == nil {
			var progress Progress
			if err := json.NewDecoder(strings.NewReader(line)).Decode(&progress); err != nil {
				log.Fatal(err)
			}
			h.progressMutex.Lock()
			h.progress[uuid] = progress
			h.progressMutex.Unlock()
			line, err = reader.ReadString('\n')
		}
		err = cmd.Wait()
		if err != nil {
			log.Fatal(err)
			sentry.CaptureException(err)
			sentry.Flush(time.Second * 5)
			continue
		}

		f, err := os.Open(pbfPath)
		defer f.Close()
		stat, _ := f.Stat()
		if err != nil {
			fmt.Errorf("failed to open file %q, %v", pbfPath, err)
			continue
		}

		err = os.Rename(pbfPath, filepath.Join(h.resultsDir, uuid+".osm.pbf"))
		if err != nil {
			fmt.Errorf("failed to open file %q, %v", pbfPath, err)
			continue
		}

		// os.RemoveAll(filepath.Join(h.tmpDir, uuid))

		var lastProgress Progress
		h.progressMutex.Lock()
		lastProgress = h.progress[uuid]
		delete(h.progress, uuid)
		h.progressMutex.Unlock()

		elapsed := time.Since(start).Seconds()
		lastProgress.Elapsed = elapsed
		lastProgress.Complete = true
		lastProgress.SizeBytes = stat.Size()
		completion, _ := json.Marshal(lastProgress)
		err = ioutil.WriteFile(filepath.Join(h.resultsDir, uuid), completion, 0644)
		if err != nil {
			panic(err)
		}

		fmt.Println("worker", id, "finished job", uuid, "in", elapsed)
	}
}

func (h *server) StartWorkers() {
	h.queue = make(chan Task, 512)
	h.progress = make(map[string]Progress)

	for i := 0; i < runtime.NumCPU(); i++ {
		go h.worker(i, h.queue)
	}
}

func GetSum(image image.Image, geom orb.Geometry) int {
	var covering map[maptile.Tile]bool
	for z := 0; z <= 14; z++ {
		covering, _ = tilecover.Geometry(geom, maptile.Zoom(z))
		if len(covering) > 256 {
			break
		}
	}

	sum := 0.0
	for t := range covering {
		sum += GetPixel(image, int(t.Z), int(t.X), int(t.Y))
	}

	return int(sum * 32)
}

func GetPixel(image image.Image, z int, x int, y int) float64 {
	if z < 12 {
		dz := 2 << ((12 - z) - 1)
		acc := 0.0
		for ix := x * dz; ix < x*dz+dz; ix++ {
			for iy := y * dz; iy < y*dz+dz; iy++ {
				color := image.At(int(ix), int(iy))
				red, green, _, _ := color.RGBA()
				acc += float64(int(red>>8)*256) + float64(green>>8)
			}
		}
		return acc
	} else if z == 12 {
		red, green, _, _ := image.At(x, y).RGBA()
		return float64(int(red>>8)*256) + float64(green>>8)
	} else {
		dz := 2 << ((z - 12) - 1)
		x := int(math.Floor(float64(x) / float64(dz)))
		y := int(math.Floor(float64(y) / float64(dz)))
		red, green, _, _ := image.At(x, y).RGBA()
		return float64(int(red>>8)*256+int(green>>8)) / float64(dz*dz)
	}
}

// check the filesystem for the result JSON
// if it's not started yet, return the position in the queue
func (h *server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Access-Control-Allow-Origin", "*")
	if r.Method == "POST" {
		decoder := json.NewDecoder(r.Body)

		var input Input
		err := decoder.Decode(&input)
		if err != nil {
			panic(err)
		}

		var geom orb.Geometry
		var sanitized_data json.RawMessage
		// validate input
		if input.RegionType == "geojson" {
			geojson_geom, _ := geojson.UnmarshalGeometry(input.RegionData)
			geom = geojson_geom.Geometry()
			sanitized_data, _ = geojson_geom.MarshalJSON()
		} else if input.RegionType == "bbox" {
			var coords []float64
			json.Unmarshal(input.RegionData, &coords)
			if len(coords) < 4 {
				w.WriteHeader(400)
				return
			}
			geom = orb.MultiPoint{orb.Point{coords[1], coords[0]}, orb.Point{coords[3], coords[2]}}.Bound()
			sanitized_data, _ = json.Marshal(coords[0:4])
		} else {
			w.WriteHeader(400)
			return
		}

		if GetSum(h.image, geom) > h.nodesLimit {
			w.WriteHeader(400)
			return
		}

		// todo: sanitize name
		// end validate input

		task := Task{Uuid: uuid.New().String(), SanitizedName: input.Name, SanitizedRegionType: input.RegionType, SanitizedRegionData: sanitized_data}

		select {
		case h.queue <- task:
			var progress Progress
			h.progressMutex.Lock()
			h.progress[task.Uuid] = progress
			h.progressMutex.Unlock()
			w.WriteHeader(201)
			fmt.Fprintf(w, task.Uuid)
		default:
			w.WriteHeader(503)
		}
	} else {
		if r.URL.Path == "/api" || r.URL.Path == "/api/" {
			l := len(h.queue)

			var timestamp string
			h.timestampMutex.Lock()

			if time.Since(h.lastTimestampTime).Seconds() > 10 {
				cmd := exec.Command(h.osmxExec, "query", h.osmxData, "timestamp")
				timestamp_raw, _ := cmd.Output()
				timestamp = strings.TrimSpace(string(timestamp_raw))
				h.lastTimestampTime = time.Now()
				h.lastTimestamp = timestamp
			} else {
				timestamp = h.lastTimestamp
			}
			h.timestampMutex.Unlock()

			w.Header().Set("Content-Type", "application/json")
			json.NewEncoder(w).Encode(SystemState{l, h.nodesLimit, timestamp})
		} else if r.URL.Path == "/api/nodes.png" {
			w.Header().Set("Content-Type", "image/png")
			w.Write(imageBytes)
		} else {
			parts := strings.Split(r.URL.Path, "/")
			if len(parts) != 3 || parts[0] != "" || parts[1] != "api" {
				w.WriteHeader(404)
			}
			uuid := parts[2]

			h.progressMutex.RLock()
			progress, ok := h.progress[uuid]
			h.progressMutex.RUnlock()

			if ok {
				w.Header().Set("Content-Type", "application/json")
				json.NewEncoder(w).Encode(progress)
				return
			}

			resultPath := filepath.Join(h.resultsDir, uuid)
			if _, err := os.Stat(resultPath); err == nil {
				Openfile, _ := os.Open(resultPath)
				defer Openfile.Close()
				w.Header().Set("Content-Type", "application/json")
				io.Copy(w, Openfile)
				return
			}
			w.WriteHeader(404)
		}
	}
}

func main() {
	var (
		resultsDir, tmpDir, osmxExec, osmxData, sentryDsn string
	)
	flag.StringVar(&resultsDir, "resultsDir", "", "Result directory")
	flag.StringVar(&tmpDir, "tmpDir", "", "Temporary directory")
	flag.StringVar(&osmxExec, "osmxExec", "", "OSMX executable")
	flag.StringVar(&osmxData, "osmxData", "", "OSMX database")
	flag.StringVar(&sentryDsn, "sentryDsn", "", "Sentry DSN")
	flag.Parse()

	if resultsDir == "" {
		fmt.Println("-resultsDir required")
		os.Exit(1)
	}
	if tmpDir == "" {
		fmt.Println("-tmpDir required")
		os.Exit(1)
	}
	if osmxExec == "" {
		fmt.Println("-osmxExec required")
		os.Exit(1)
	}
	if osmxData == "" {
		fmt.Println("-osmxData required")
		os.Exit(1)
	}

	if sentryDsn != "" {
		err := sentry.Init(sentry.ClientOptions{
			Dsn: sentryDsn,
		})

		if err != nil {
			fmt.Printf("Sentry initialization failed: %v\n", err)
		}
	}

	img, err := png.Decode(bytes.NewReader(imageBytes))
	if err != nil {
		fmt.Println("Error decoding file:", err)
		return
	}

	srv := server{
		resultsDir: resultsDir,
		tmpDir:     tmpDir,
		osmxExec:   osmxExec,
		osmxData:   osmxData,
		image:      img,
		nodesLimit: 100000000,
	}
	srv.StartWorkers()
	http.Handle("/", &srv)
	log.Fatal(http.ListenAndServe(":8080", nil))
}
