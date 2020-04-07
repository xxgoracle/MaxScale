require("../test_utils.js")();

var tests = [
  "list servers",
  "list services",
  "list listeners RW-Split-Router",
  "list monitors",
  "list sessions",
  "list filters",
  "list modules",
  "list users",
  "list commands",
  "list threads",
  "show servers",
  "show services",
  "show monitors",
  "show sessions",
  "show session 1",
  "show filters",
  "show modules",
  "show maxscale",
  "show logging",
  "show threads",
  "show thread 0",
  "show server server1",
  "show service RW-Split-Router",
  "show monitor MariaDB-Monitor",
  "show sessions",
  "show filter Hint",
  "show module readwritesplit",
  "show maxscale",
  "show logging",
  "show commands readwritesplit",
  "show qc_cache",
  "show dbusers RW-Split-Router",
];

var rdns_tests = ["list sessions", "show sessions", "show session 1"];

describe("Diagnostic Commands", function () {
  before(startMaxScale);
  before(createConnection);

  tests.forEach(function (i) {
    it(i, function () {
      return doCommand(i).should.be.fulfilled;
    });
  });

  rdns_tests.forEach(function (i) {
    it(i + " with reverse DNS lookups", function () {
      return doCommand(i + " --rdns").should.be.fulfilled;
    });
  });

  after(closeConnection);
  after(stopMaxScale);
});

describe("MXS-1656: `list servers` with GTIDs", function () {
  before(startMaxScale);

  doCheck = function () {
    return doCommand("list servers --tsv").then((res) => {
      // Check that at least 5 columns are returned with the last column consisting of
      // empty strings. This is because the test setup uses file and position based
      // replication.
      res = res
        .split("\n")
        .map((i) => i.split("\t"))
        .map((i) => i[5]);
      var pos = _.uniq(res)[0];
      if (pos.length) {
        pos.should.match(/[0-9]*-[0-9]*-[0-9]*/);
      } else {
        pos.should.equal("");
      }
    });
  };

  it("Lists monitored servers", function () {
    return doCheck();
  });

  it("Lists unmonitored servers", function () {
    return doCommand("unlink monitor MariaDB-Monitor server1 server2 server3 server4").then(() => doCheck());
  });

  it("Lists partially monitored servers", function () {
    return doCommand("link monitor MariaDB-Monitor server1 server3").then(() => doCheck());
  });

  after(stopMaxScale);
});
