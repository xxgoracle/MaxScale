require("../test_utils.js")();

describe("Set/Clear Commands", function () {
  before(function () {
    return startMaxScale().then(function () {
      return request.put(host + "monitors/MariaDB-Monitor/stop", {
        auth: { user: "admin", password: "mariadb" },
      });
    });
  });

  it("set correct state", function () {
    return verifyCommand("set server server2 master", "servers/server2").then(function (res) {
      res.data.attributes.state.should.match(/Master/);
    });
  });

  it("clear correct state", function () {
    return verifyCommand("clear server server2 master", "servers/server2").then(function (res) {
      res.data.attributes.state.should.not.match(/Master/);
    });
  });

  it("force maintenance mode", function () {
    return verifyCommand("set server server1 maintenance --force", "servers/server1").then(function (res) {
      res.data.attributes.state.should.match(/Maintenance/);
    });
  });

  it("clear maintenance mode", function () {
    return verifyCommand("clear server server1 maintenance", "servers/server1").then(function (res) {
      res.data.attributes.state.should.not.match(/Maintenance/);
    });
  });

  it("reject set incorrect state", function () {
    return doCommand("set server server2 something").should.be.rejected;
  });

  it("reject clear incorrect state", function () {
    return doCommand("clear server server2 something").should.be.rejected;
  });

  after(stopMaxScale);
});
