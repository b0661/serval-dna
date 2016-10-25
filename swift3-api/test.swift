import ServalClient
import Dispatch
#if os(Linux)
import Glibc
#endif

var arg0 : String = ""

func usage() {
    print("Usage: \(arg0) keyring list")
    print("       \(arg0) keyring add [ did [ name [ pin ] ] ]")
    print("       \(arg0) keyring remove sid [ pin ]")
    print("       \(arg0) keyring set sid [ did [ name [ pin ] ] ]")
}

func main() {
    var args = CommandLine.arguments
    arg0 = args.remove(at: 0)
    var port : Int16?
    var username : String?
    var password : String?
    optLoop: while (!args.isEmpty) {
        let arg = args[0]
        var opt : String
        var param : String?
        var optrange : Range<Int>
        if let eq = arg.range(of: "=") {
            opt = arg.substring(to: eq.lowerBound)
            param = arg.substring(from: eq.upperBound)
            optrange = 0 ..< 1
        }
        else {
            opt = arg
            param = args.count > 1 ? args[1] : nil
            optrange = 0 ..< 2
        }
        switch (opt) {
        case "--port":
            port = Int16(param!)
            args.removeSubrange(optrange)
        case "--user":
            username = param!
            args.removeSubrange(optrange)
        case "--password":
            password = param!
            args.removeSubrange(optrange)
        default:
            break optLoop
        }
    }
    let restful_config = ServalRestfulClient.Configuration.default.with(port: port, username: username, password: password)
    let cmd = args.isEmpty ? "" : args.remove(at: 0)
    switch (cmd) {
    case "keyring":
        exit(keyring(&args, configuration: restful_config))
    default:
        usage()
        exit(1)
    }
}

func keyring(_ args: inout [String], configuration: ServalRestfulClient.Configuration) -> Int32 {
    let cmd = args.isEmpty ? "" : args.remove(at: 0)
    switch (cmd) {
    case "list":
        print("4")
        print("sid:identity:did:name")
        let semaphore = DispatchSemaphore(value: 0)
        let client = ServalRestfulClient(configuration: configuration)
        let request = ServalKeyring.listIdentities(client: client) { (identities, error) in
            if let error = error {
                print(error)
            }
            else if let identities = identities {
                for identity in identities {
                    print("\(identity.sid.hexUpper):\(identity.identity.hexUpper):\(identity.did):\(identity.name)")
                }
            }
            semaphore.signal()
        }
        print("Waiting...")
        semaphore.wait()
        print("Done")
        request.close()

    case "add":
        let did = args.isEmpty ? "" : args.remove(at: 0)
        let name = args.isEmpty ? "" : args.remove(at: 0)
        let pin = args.isEmpty ? "" : args.remove(at: 0)
        var message = "Adding (did="
        debugPrint(did, terminator:"", to:&message)
        message += " name="
        debugPrint(name, terminator:"", to:&message)
        message += " pin="
        debugPrint(pin, terminator:"", to:&message)
        message += ")..."
        print(message)
        let semaphore = DispatchSemaphore(value: 0)
        let client = ServalRestfulClient(configuration: configuration)
        let request = ServalKeyring.addIdentity(client: client, did: did, name: name, pin: pin) { (identity, error) in
            if let error = error {
                print(error)
            }
            else if let identity = identity {
                print("\(identity.sid.hexUpper):\(identity.identity.hexUpper):\(identity.did):\(identity.name)")
            }
            semaphore.signal()
        }
        print("Waiting...")
        semaphore.wait()
        print("Done")
        request.close()

    case "remove":
        let sid = SubscriberId(fromHex: args.remove(at: 0))!
        print("Removing (sid=\(sid.hexUpper))...")
        let semaphore = DispatchSemaphore(value: 0)
        let client = ServalRestfulClient(configuration: configuration)
        let request = ServalKeyring.removeIdentity(client: client, sid: sid) { (identity, error) in
            if let error = error {
                print(error)
            }
            else if let identity = identity {
                print("\(identity.sid.hexUpper):\(identity.identity.hexUpper):\(identity.did):\(identity.name)")
            }
            semaphore.signal()
        }
        print("Waiting...")
        semaphore.wait()
        print("Done")
        request.close()

    case "set":
        let sid = SubscriberId(fromHex: args.remove(at: 0))!
        let did = args.isEmpty ? "" : args.remove(at: 0)
        let name = args.isEmpty ? "" : args.remove(at: 0)
        let pin = args.isEmpty ? "" : args.remove(at: 0)
        var message = "Setting (sid=\(sid.hexUpper))..."
        message += " did="
        debugPrint(did, terminator:"", to:&message)
        message += " name="
        debugPrint(name, terminator:"", to:&message)
        message += " pin="
        debugPrint(pin, terminator:"", to:&message)
        let semaphore = DispatchSemaphore(value: 0)
        let client = ServalRestfulClient(configuration: configuration)
        let request = ServalKeyring.setIdentity(client: client, sid: sid, did: did, name: name, pin: pin) { (identity, error) in
            if let error = error {
                print(error)
            }
            else if let identity = identity {
                print("\(identity.sid.hexUpper):\(identity.identity.hexUpper):\(identity.did):\(identity.name)")
            }
            semaphore.signal()
        }
        print("Waiting...")
        semaphore.wait()
        print("Done")
        request.close()

    default:
        usage()
        return 1
    }
    return 0
}

main()
