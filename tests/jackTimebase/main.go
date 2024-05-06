package main

import (
    "context"
    "log"
    "os"
    "os/exec"
    "path"
    "strconv"
    "time"

    "github.com/hypebeast/go-osc/osc"
)

const oscHydrogenPort = 9099
const oscTestBinaryPort = 8099
const oscTestBinaryListenerPort = 8100

// hydrogenStartupTime gives an upper limit for the time Hydrogen requires to
// start up in milliseconds.
const hydrogenStartupTime = 5000
// hydrogenTearDownTime gives an upper limit for the time required for
// killHydrogen() to send a quit OSC signal, for Hydrogen to receive it and
// finish its tear down.
const hydrogenTearDownTime = 3000

// oscCommandTime defines the time in milliseconds the tests do wait after
// sending an OSC command before going on.
const oscCommandTime = 500

var hydrogenLogFile = "./hydrogen.log"
var testBinaryLogFileBase = "./test"

// hydrogenTestSongPath points to the song opened by the Hydrogen binary used as
// counterpart to our dedicated test binary.
var hydrogenTestSongPath = "jackTimebaseTest.h2song"

// hydrogenFailedChan is used by startHydrogen() to indicate that Hydrogen exited.
var hydrogenFailedChan chan bool
// testBinaryFailedChan is used by startTestBinary() to indicate it exited.
var testBinaryFailedChan chan bool
// nextTestChan is used by startTestBinary() to indicate it exited.
var nextTestChan chan bool
var finishTestChan chan bool

var hydrogenPath = path.Join(
    "..", "..", "build", "src", "cli", "h2cli")
var testBinaryPath = path.Join(
    "..", "..", "build", "tests", "jackTimebase", "h2JackTimebase", "h2JackTimebase")

var hydrogenClient *osc.Client
var testBinaryClient *osc.Client
var testBinaryListenerClient *osc.Client

var testNumber int

var returnCode int

// Integration test checking for errors/crashes when closing Hydrogen while
// using the JACK driver (on Linux).
func main() {
    var err error

    _, err = exec.LookPath(hydrogenPath)
    if err != nil {
        log.Fatalf("[%v] executable could not be found: %v", hydrogenPath,
            err.Error())
    }
    _, err = exec.LookPath(testBinaryPath)
    if err != nil {
        log.Fatalf("[%v] executable could not be found: %v", testBinaryPath,
            err.Error())
    }

    hydrogenFailedChan = make(chan bool, 1)
    testBinaryFailedChan = make(chan bool, 1)
    nextTestChan = make(chan bool, 1)
    finishTestChan = make(chan bool, 1)

    // Trigger first test
    nextTestChan <- true
    testNumber = 0
    returnCode = 0

    testContext, testContextCancel := context.WithCancel(context.Background())

    hydrogenClient = osc.NewClient("localhost", oscHydrogenPort)
    testBinaryClient = osc.NewClient("localhost", oscTestBinaryPort)
    testBinaryListenerClient = osc.NewClient("localhost", oscTestBinaryListenerPort)

    go startHydrogen(testContext)

    mainLoop(testContext)

    log.Println("Initiating teardown...")

    // Teardown
    sendMsg(hydrogenClient, osc.NewMessage("/Hydrogen/QUIT"))
    sendMsg(testBinaryClient, osc.NewMessage("/Hydrogen/QUIT"))
    sendMsg(testBinaryListenerClient, osc.NewMessage("/Hydrogen/QUIT"))

    // Give both applications time to shut down gracefully
    time.Sleep(hydrogenTearDownTime * time.Millisecond)

    testContextCancel()

    time.Sleep(100 * time.Millisecond)

    os.Exit( returnCode )
 }

func mainLoop(ctx context.Context) {
    for {
        select {
        case <-hydrogenFailedChan:
            // Hydrogen exited
            log.Println("hydrogenFailedChan")
            returnCode = 1
            return
        case <-testBinaryFailedChan:
            // Test binary exits
            log.Println("testBinaryFailedChan")
            returnCode = 1
            return
        case <-nextTestChan:
            // Integration test succeeded
            nextTest(ctx)

        case <-finishTestChan:
            // Done
            return

        default:
            time.Sleep(100 * time.Millisecond)
            continue
        }
    }
}

func sendMsg(c *osc.Client, m *osc.Message) {

    log.Printf("[sendMsg] sending message [%v] to [:%v]",
        m.String(), c.Port())

    c.Send(m)

    // Wait for Hydrogen to receive and handle the message.
    time.Sleep(oscCommandTime * time.Millisecond)
}

func startHydrogen(ctx context.Context) {
    cmd := exec.CommandContext(ctx, hydrogenPath, "--driver", "jack",
        "-s", hydrogenTestSongPath, "-O", strconv.FormatInt(oscHydrogenPort, 10),
        "-L", hydrogenLogFile, "-T", "-V", "Debug")
    output, err := cmd.CombinedOutput()
    if err == nil {
        log.Printf("[startHydrogen] [%v] exited", cmd.String())
    } else {
        log.Printf("[startHydrogen] ERROR: [%v] exited with error: %v",
            cmd.String(), err)
        log.Printf("[startTestBinary] stdout/stderr: %v", string(output))
        hydrogenFailedChan <- true
    }
}

func startTestBinary(ctx context.Context, logFileSuffix string, oscPort int64) {

    log.Println("[nextTest] Starting test binary...")

    cmd := exec.CommandContext(ctx, testBinaryPath,
        "-L", testBinaryLogFileBase + "-" + logFileSuffix + ".log",
        "-s", hydrogenTestSongPath,
        "-O", strconv.FormatInt(oscPort, 10), "-V", "Debug")
    output, err := cmd.CombinedOutput()
    if err == nil {
        log.Println("[startTestBinary] SUCCESS!")
        log.Println("")
        nextTestChan <- true
    } else {
        log.Printf("[startTestBinary] ERROR: [%v] exited with error: %v",
            cmd.String(), err)
        log.Printf("[startTestBinary] stdout/stderr: %v", string(output))
        log.Println("")
        testBinaryFailedChan <- true
    }
}

func nextTest(ctx context.Context) {
    switch(testNumber) {
    case 0:
        go startTestBinary(ctx, "non-timebase", oscTestBinaryPort)
        // Wait for the test binary to be ready.
        time.Sleep(hydrogenStartupTime * time.Millisecond)

        go runNonTimebaseTestSuite()

    case 1:
        go startTestBinary(ctx, "timebase-master", oscTestBinaryPort)
        // Wait for the test binary to be ready.
        time.Sleep(hydrogenStartupTime * time.Millisecond)

        go runTimebaseMasterTestSuite()

    case 2:
        go startTestBinary(ctx, "timebase-listener", oscTestBinaryPort)
        // Wait for the test binary to be ready.
        time.Sleep(hydrogenStartupTime * time.Millisecond)

        go runTimebaseListenerTestSuite()

    case 3:
        // Start a dedicated test binary instead of the main application. We
        // want to test whether relocations happening within the master or
        // properly handled when received by the listener. For this we need two
        // applications with patched JACK process callback.
        sendMsg(hydrogenClient, osc.NewMessage("/Hydrogen/QUIT"))

        go startTestBinary(ctx, "bbt-relocation-listener",
            oscTestBinaryListenerPort)
        go startTestBinary(ctx, "bbt-relocation-master", oscTestBinaryPort)
        // Wait for the test binary to be ready.
        time.Sleep(hydrogenStartupTime * time.Millisecond)

        go runBBTRelocationTestSuite()

    default:
        log.Println("[nextTest] No test left. Exiting...")
        finishTestChan <- true
    }

    testNumber += 1
}

func runNonTimebaseTestSuite() {
    log.Println("")
    log.Println("[nextTest] Running non-Timebase test suite.")
    log.Println("[nextTest] Test binary is run next to Hydrogen and none of them has JACK Timebase support enabled.")
    log.Println("")

    // run transport tests without JACK Timebase
    sendMsg(hydrogenClient,
        osc.NewMessage("/Hydrogen/JACK_TIMEBASE_MASTER_ACTIVATION", float64(0)))
    sendMsg(testBinaryClient,
        osc.NewMessage("/Hydrogen/JACK_TIMEBASE_MASTER_ACTIVATION", float64(0)))
    sendMsg(testBinaryClient, osc.NewMessage("/h2JackTimebase/TransportTests"))
}

func runTimebaseListenerTestSuite() {
    log.Println("")
    log.Println("[nextTest] Running Timebase test suite as listener.")
    log.Println("[nextTest] Test binary is run next to Hydrogen and the latter is registered as JACK Timebase master.")
    log.Println("")

    // run transport tests without JACK Timebase
    sendMsg(hydrogenClient,
        osc.NewMessage("/Hydrogen/JACK_TIMEBASE_MASTER_ACTIVATION", float64(1)))
    sendMsg(testBinaryClient,
        osc.NewMessage("/Hydrogen/JACK_TIMEBASE_MASTER_ACTIVATION", float64(0)))
    sendMsg(testBinaryClient, osc.NewMessage("/h2JackTimebase/TransportTests"))
}

func runTimebaseMasterTestSuite() {
    log.Println("")
    log.Println("[nextTest] Running Timebase test suite as Master.")
    log.Println("[nextTest] Test binary is run next to Hydrogen and the former is registered as JACK Timebase master.")
    log.Println("")

    // run transport tests without JACK Timebase
    sendMsg(hydrogenClient,
        osc.NewMessage("/Hydrogen/JACK_TIMEBASE_MASTER_ACTIVATION", float64(0)))
    sendMsg(testBinaryClient,
        osc.NewMessage("/Hydrogen/JACK_TIMEBASE_MASTER_ACTIVATION", float64(1)))
    sendMsg(testBinaryClient, osc.NewMessage("/h2JackTimebase/TransportTests"))
}

func runBBTRelocationTestSuite() {
    log.Println("")
    log.Println("[nextTest] Running Timebase test suite as both Master and Listener.")
    log.Println("[nextTest] Test whether relocation in the Timebase master are handled properly in the listener.")
    log.Println("")

    // run transport tests without JACK Timebase
    sendMsg(testBinaryListenerClient,
        osc.NewMessage("/Hydrogen/JACK_TIMEBASE_MASTER_ACTIVATION", float64(0)))
    sendMsg(testBinaryListenerClient,
        osc.NewMessage("/Hydrogen/StartTestJackDriver"))
    sendMsg(testBinaryClient,
        osc.NewMessage("/Hydrogen/JACK_TIMEBASE_MASTER_ACTIVATION", float64(1)))
    sendMsg(testBinaryClient, osc.NewMessage("/h2JackTimebase/TransportTests"))
}
